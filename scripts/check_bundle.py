#!/usr/bin/env python3
"""Offline integrity check for the FRED skill bundle and its evidence records.

Bounded contract (see docs/architecture.md):

* Parses SKILL.md frontmatter with a documented *restricted* grammar. This is
  not a YAML implementation; anything outside the grammar is rejected.
* Checks names, required sections, content budgets, explicit shared-reference
  links, local Markdown links/anchors, symlink and traversal escapes.
* Validates the claim registry (sources.json), compatibility.json and the
  evaluation definitions structurally.

It never evaluates input (no eval/exec/import of checked files), never spawns
processes, never touches the network, and installs nothing. Passing this check
says nothing about the correctness of C code, SDK builds or device behaviour.

Exit status: 0 = no errors, 1 = errors found, 2 = usage or unreadable root.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import sys
from pathlib import Path

SKILLS = ("flipper-core", "flipper-native", "flipper-review", "flipper-verify")
CORE = "flipper-core"
SHARED_REQUIRED = (
    "references/engineering-rules.md",
    "references/evidence-policy.md",
)
CORE_FILES = (
    "references/engineering-rules.md",
    "references/evidence-policy.md",
    "references/compatibility.md",
    "references/capability-map.md",
    "references/runtime-ui.md",
    "references/storage-parsers.md",
    "references/hardware-communications.md",
    "references/build-debug-release.md",
    "references/assumptions.md",
    "references/sources.json",
    "assets/compatibility-card.md",
    "assets/ownership-table.md",
    "assets/validation-report.md",
    "assets/device-test-plan.md",
)
REQUIRED_SECTIONS = (
    "Purpose",
    "Activation",
    "Inputs and prerequisites",
    "Workflow",
    "References",
    "Outputs and completion",
    "Validation",
    "Compatibility boundaries",
    "Failure modes",
    "Handoffs",
)
ALLOWED_KEYS = ("name", "description", "license", "compatibility", "metadata")
FORBIDDEN_KEYS = ("allowed-tools", "hooks", "context", "agent", "model")
NAME_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
KEY_RE = re.compile(r"^([a-z][a-z0-9_-]*):(?: (.*))?$")
META_RE = re.compile(r"^  ([a-z][a-z0-9_-]*): (.+)$")
RESERVED_NAME_WORDS = ("anthropic", "claude")
# Values that a YAML parser would interpret as something other than a plain
# string, or that are ambiguous under our grammar, are rejected.
UNSAFE_VALUE_START = tuple("'\"[]{}&*!|>%@`#,?-")
DYNAMIC_PATTERNS = ("!`", "${", "$(", "$ARGUMENTS", "{{")

MAX_SKILL_LINES = 150
MAX_SKILL_WORDS = 1000
MAX_DESCRIPTIONS_WORDS = 300
MAX_RULES_WORDS = 1200
MAX_NAME_LEN = 64
MAX_DESCRIPTION_CHARS = 1024
MAX_COMPAT_CHARS = 500
MAX_FILE_BYTES = 2_000_000

CLAIM_STATUS = ("documented", "source-confirmed", "inferred", "unresolved")
REVIEW_STATES = ("current", "review-required", "deprecated", "unsupported")
CHECK_STATUS = ("pass", "fail", "not-run", "blocked", "not-applicable")
EVIDENCE_FIELDS = (
    "documentation_reviewed",
    "source_inspected",
    "static_checks",
    "compiled",
    "host_tests",
    "device_behavior",
)
SUPPORT_LEVELS = (
    "researched",
    "feasibility-only",
    "compiled",
    "host-tested",
    "device-verified",
    "unsupported",
)
SUPPORT_REQUIRES = {
    "compiled": ("compiled",),
    "host-tested": ("compiled", "host_tests"),
    "device-verified": ("compiled", "device_behavior"),
}
CLAIM_ID_RE = re.compile(r"^C-[A-Z]+-\d{3}$")
CLAIM_CITE_RE = re.compile(r"\bC-[A-Z]+-\d{3}\b")
SHA40_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
PINNED_HOSTS = ("github.com/", "raw.githubusercontent.com/")
MOVING_REFS = ("/blob/dev/", "/tree/dev/", "/blob/main/", "/tree/main/",
               "/blob/master/", "/tree/master/", "/blob/release/", "/tree/release/")
LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def error(self, where: str, msg: str) -> None:
        self.errors.append(f"{where}: {msg}")

    def warn(self, where: str, msg: str) -> None:
        self.warnings.append(f"{where}: {msg}")


def rel(path: Path, base: Path) -> str:
    try:
        return path.relative_to(base).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path, rep: Report, where: str) -> str | None:
    try:
        if path.stat().st_size > MAX_FILE_BYTES:
            rep.error(where, f"file exceeds {MAX_FILE_BYTES} bytes")
            return None
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        rep.error(where, "not valid UTF-8")
    except OSError as exc:
        rep.error(where, f"unreadable ({exc.strerror})")
    return None


def word_count(text: str) -> int:
    return len(text.split())


# ---------------------------------------------------------------------------
# Restricted frontmatter grammar
# ---------------------------------------------------------------------------

def parse_frontmatter(text: str) -> tuple[dict, str, list[str]]:
    """Return (fields, body, errors). Never interprets values beyond strings."""
    errors: list[str] = []
    if text.startswith("﻿"):
        errors.append("byte-order mark before frontmatter")
        text = text[1:]
    lines = text.split("\n")
    if not lines or lines[0].rstrip("\r") != "---":
        return {}, text, ["file must start with a '---' frontmatter line"]
    end = None
    for i in range(1, len(lines)):
        if lines[i].rstrip("\r") == "---":
            end = i
            break
    if end is None:
        return {}, text, ["frontmatter is not closed by a '---' line"]
    fields: dict = {}
    current_map: str | None = None
    for lineno, raw in enumerate(lines[1:end], start=2):
        line = raw.rstrip("\r")
        if "\t" in line:
            errors.append(f"line {lineno}: tab characters are not allowed")
            continue
        if not line.strip():
            errors.append(f"line {lineno}: blank lines are not allowed in frontmatter")
            continue
        if line.lstrip().startswith("#"):
            errors.append(f"line {lineno}: comments are not allowed in frontmatter")
            continue
        if line.startswith(" "):
            m = META_RE.match(line)
            if current_map is None or not m:
                errors.append(f"line {lineno}: unexpected indentation")
                continue
            key, value = m.group(1), m.group(2)
            verr = check_value(value)
            if verr:
                errors.append(f"line {lineno}: metadata.{key} {verr}")
                continue
            if key in fields[current_map]:
                errors.append(f"line {lineno}: duplicate key metadata.{key}")
                continue
            fields[current_map][key] = value
            continue
        current_map = None
        m = KEY_RE.match(line)
        if not m:
            errors.append(f"line {lineno}: expected 'key: value'")
            continue
        key, value = m.group(1), m.group(2)
        if key in fields:
            errors.append(f"line {lineno}: duplicate key '{key}'")
            continue
        if key in FORBIDDEN_KEYS:
            errors.append(f"line {lineno}: key '{key}' is forbidden in this bundle")
            continue
        if key not in ALLOWED_KEYS:
            errors.append(f"line {lineno}: unknown key '{key}'")
            continue
        if key == "metadata":
            if value not in (None, ""):
                errors.append(f"line {lineno}: metadata must be an indented mapping")
                continue
            fields[key] = {}
            current_map = key
            continue
        if value is None or value.strip() == "":
            errors.append(f"line {lineno}: '{key}' has an empty value")
            continue
        verr = check_value(value)
        if verr:
            errors.append(f"line {lineno}: '{key}' {verr}")
            continue
        fields[key] = value
    if "metadata" in fields and not fields["metadata"]:
        errors.append("metadata mapping is empty")
    body = "\n".join(lines[end + 1:])
    return fields, body, errors


def check_value(value: str) -> str | None:
    if value != value.strip():
        return "has leading or trailing whitespace"
    if value.startswith(UNSAFE_VALUE_START):
        return f"starts with an ambiguous character {value[0]!r}"
    if ": " in value or value.endswith(":"):
        return "contains ': ' (ambiguous mapping)"
    if " #" in value:
        return "contains ' #' (ambiguous comment)"
    for pat in DYNAMIC_PATTERNS:
        if pat in value:
            return f"contains dynamic pattern {pat!r}"
    if value.lower() in ("true", "false", "yes", "no", "on", "off", "null", "~"):
        return "is a YAML keyword, not a plain string"
    if re.fullmatch(r"[-+]?\d[\d_.eE+-]*", value):
        return "looks numeric, not a plain string"
    return None


# ---------------------------------------------------------------------------
# Links
# ---------------------------------------------------------------------------

def slugify(heading: str) -> str:
    text = heading.strip().lower()
    text = re.sub(r"[`*_~]", "", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def anchors_of(text: str) -> set[str]:
    found: set[str] = set()
    counts: dict[str, int] = {}
    in_fence = False
    for line in text.split("\n"):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
        if m:
            slug = slugify(m.group(2))
            n = counts.get(slug, 0)
            counts[slug] = n + 1
            found.add(slug if n == 0 else f"{slug}-{n}")
    return found


def iter_links(text: str):
    in_fence = False
    for lineno, line in enumerate(text.split("\n"), start=1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        stripped = re.sub(r"`[^`]*`", "", line)
        for m in LINK_RE.finditer(stripped):
            yield lineno, m.group(1)


def is_within(path: Path, boundary: Path) -> bool:
    try:
        path.relative_to(boundary)
        return True
    except ValueError:
        return False


def check_links(path: Path, text: str, boundary: Path, base: Path, rep: Report,
                anchor_cache: dict) -> None:
    where = rel(path, base)
    for lineno, target in iter_links(text):
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
            continue  # external URL (http, https, mailto); not fetched
        file_part, _, anchor = target.partition("#")
        if file_part == "":
            dest = path
        else:
            if file_part.startswith("/") or "\\" in file_part:
                rep.error(f"{where}:{lineno}", f"absolute or backslash link '{target}'")
                continue
            dest = (path.parent / file_part)
        lexical = Path(os.path.normpath(dest))
        if not is_within(lexical, boundary):
            rep.error(f"{where}:{lineno}", f"link escapes {rel(boundary, base) or '.'}: '{target}'")
            continue
        if not lexical.exists():
            rep.error(f"{where}:{lineno}", f"broken link '{target}'")
            continue
        real = lexical.resolve()
        if not is_within(real, boundary.resolve()):
            rep.error(f"{where}:{lineno}", f"link resolves outside boundary via symlink: '{target}'")
            continue
        if anchor and lexical.suffix == ".md" and lexical.is_file():
            key = str(real)
            if key not in anchor_cache:
                t = read_text(lexical, Report(), "")
                anchor_cache[key] = anchors_of(t or "")
            if anchor not in anchor_cache[key]:
                rep.error(f"{where}:{lineno}", f"missing anchor '#{anchor}' in '{file_part or path.name}'")


def check_symlinks(root: Path, boundary: Path, base: Path, rep: Report) -> None:
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        for name in dirnames + filenames:
            p = Path(dirpath) / name
            if p.is_symlink():
                target = p.resolve()
                if not is_within(target, boundary.resolve()):
                    rep.error(rel(p, base), "symlink points outside the bundle")
                else:
                    rep.warn(rel(p, base), "symlink inside bundle; copies may not preserve it")


# ---------------------------------------------------------------------------
# Skills
# ---------------------------------------------------------------------------

def check_skills(skills_dir: Path, base: Path, rep: Report, anchor_cache: dict) -> dict:
    descriptions: dict[str, str] = {}
    if not skills_dir.is_dir():
        rep.error(rel(skills_dir, base), "skills directory not found")
        return descriptions
    check_symlinks(skills_dir, skills_dir, base, rep)
    present = sorted(p.name for p in skills_dir.iterdir() if p.is_dir() and p.name.startswith("flipper-"))
    for name in SKILLS:
        if name not in present:
            rep.error(rel(skills_dir, base),
                      f"broken installation: sibling skill '{name}' is missing; "
                      "install all four FRED skill directories together")
    for extra in present:
        if extra not in SKILLS:
            rep.warn(rel(skills_dir / extra, base), "unexpected flipper-* directory (not part of the suite)")
    core = skills_dir / CORE
    for f in CORE_FILES:
        if not (core / f).is_file():
            rep.error(rel(core / f, base), "broken installation: required shared file is missing")
    for name in SKILLS:
        skill_md = skills_dir / name / "SKILL.md"
        if not skill_md.is_file():
            if (skills_dir / name).is_dir():
                rep.error(rel(skill_md, base), "SKILL.md missing")
            continue
        text = read_text(skill_md, rep, rel(skill_md, base))
        if text is None:
            continue
        where = rel(skill_md, base)
        fields, body, errs = parse_frontmatter(text)
        for e in errs:
            rep.error(where, f"frontmatter {e}")
        sname = fields.get("name")
        if not sname:
            rep.error(where, "frontmatter 'name' is required")
        else:
            if not NAME_RE.match(sname) or len(sname) > MAX_NAME_LEN:
                rep.error(where, f"invalid name '{sname}'")
            if sname != name:
                rep.error(where, f"name '{sname}' does not match directory '{name}'")
            for word in RESERVED_NAME_WORDS:
                if word in sname:
                    rep.error(where, f"name contains reserved word '{word}'")
        desc = fields.get("description")
        if not desc:
            rep.error(where, "frontmatter 'description' is required")
        else:
            if len(desc) > MAX_DESCRIPTION_CHARS:
                rep.error(where, f"description exceeds {MAX_DESCRIPTION_CHARS} characters")
            if "<" in desc or ">" in desc:
                rep.error(where, "description must not contain angle brackets")
            descriptions[name] = desc
        compat = fields.get("compatibility")
        if compat and len(compat) > MAX_COMPAT_CHARS:
            rep.error(where, f"compatibility exceeds {MAX_COMPAT_CHARS} characters")
        lines = text.rstrip("\n").count("\n") + 1
        if lines > MAX_SKILL_LINES:
            rep.error(where, f"{lines} lines exceeds budget {MAX_SKILL_LINES}")
        words = word_count(text)
        if words > MAX_SKILL_WORDS:
            rep.error(where, f"{words} words exceeds budget {MAX_SKILL_WORDS}")
        for pat in DYNAMIC_PATTERNS:
            if pat in body:
                rep.error(where, f"body contains dynamic pattern {pat!r}")
        headings = {m.group(1).strip() for m in re.finditer(r"^##\s+(.+)$", body, re.M)}
        for section in REQUIRED_SECTIONS:
            if section not in headings:
                rep.error(where, f"missing required section '## {section}'")
        link_targets = {t for _, t in iter_links(body)}
        if name != CORE:
            for f in SHARED_REQUIRED:
                needed = f"../{CORE}/{f}"
                if needed not in link_targets:
                    rep.error(where, f"must explicitly link shared reference '{needed}'")
            if "broken installation" not in body.lower():
                rep.error(where, "must state the missing-shared-file installation error")
        else:
            for f in SHARED_REQUIRED:
                if f not in link_targets:
                    rep.error(where, f"core must link its shared reference '{f}'")
        if "evaluations/" in text:
            rep.error(where, "skill payload must not reference evaluation oracles or fixtures")
    # Every Markdown file in the bundle: links stay inside the suite.
    for p in sorted(skills_dir.rglob("*.md")):
        if p.is_symlink():
            continue
        t = read_text(p, rep, rel(p, base))
        if t is None:
            continue
        check_links(p, t, skills_dir, base, rep, anchor_cache)
        if "evaluations/" in t:
            rep.error(rel(p, base), "skill payload must not reference evaluation oracles or fixtures")
    total = sum(word_count(d) for d in descriptions.values())
    if total > MAX_DESCRIPTIONS_WORDS:
        rep.error(rel(skills_dir, base), f"combined descriptions {total} words exceeds {MAX_DESCRIPTIONS_WORDS}")
    rules = core / "references" / "engineering-rules.md"
    if rules.is_file():
        t = read_text(rules, rep, rel(rules, base)) or ""
        w = word_count(t)
        if w > MAX_RULES_WORDS:
            rep.error(rel(rules, base), f"{w} words exceeds budget {MAX_RULES_WORDS}")
    return descriptions


# ---------------------------------------------------------------------------
# Claim registry
# ---------------------------------------------------------------------------

def parse_date(value, where: str, field: str, rep: Report):
    if not isinstance(value, str) or not DATE_RE.match(value):
        rep.error(where, f"'{field}' must be an ISO date YYYY-MM-DD")
        return None
    try:
        return _dt.date.fromisoformat(value)
    except ValueError:
        rep.error(where, f"'{field}' is not a valid date")
        return None


def load_json(path: Path, rep: Report, base: Path):
    where = rel(path, base)
    text = read_text(path, rep, where)
    if text is None:
        return None

    def no_dupes(pairs):
        obj = {}
        for k, v in pairs:
            if k in obj:
                raise ValueError(f"duplicate key '{k}'")
            obj[k] = v
        return obj

    try:
        return json.loads(text, object_pairs_hook=no_dupes)
    except ValueError as exc:
        rep.error(where, f"invalid JSON: {exc}")
        return None


def check_url(url, where: str, rep: Report, live: bool) -> None:
    if not isinstance(url, str) or not url.startswith("https://"):
        rep.error(where, "source url must be an https URL")
        return
    if any(h in url for h in PINNED_HOSTS) and not live:
        if any(m in url for m in MOVING_REFS):
            rep.error(where, f"moving branch reference in pinned source: {url}")
        elif not re.search(r"/[0-9a-f]{40}(?:/|#|$)", url) and "/releases/tag/" not in url:
            rep.error(where, f"repository source is not commit-pinned (mark live=true if intentional): {url}")


def check_sources(skills_dir: Path, base: Path, rep: Report, today: _dt.date,
                  max_age_days: int) -> dict:
    path = skills_dir / CORE / "references" / "sources.json"
    if not path.is_file():
        return {}
    data = load_json(path, rep, base)
    where = rel(path, base)
    if not isinstance(data, dict):
        if data is not None:
            rep.error(where, "top level must be an object")
        return {}
    if data.get("schema_version") != 1:
        rep.error(where, "schema_version must be 1")
    pins = data.get("pins")
    if not isinstance(pins, dict) or not pins:
        rep.error(where, "'pins' must be a non-empty object")
        pins = {}
    for pname, pin in pins.items():
        pw = f"{where}: pins.{pname}"
        if not isinstance(pin, dict) or not isinstance(pin.get("repo"), str):
            rep.error(pw, "pin needs 'repo'")
            continue
        if not SHA40_RE.match(str(pin.get("commit", ""))):
            rep.error(pw, "pin needs a full 40-hex 'commit'")
    claims = data.get("claims")
    if not isinstance(claims, list) or not claims:
        rep.error(where, "'claims' must be a non-empty list")
        return {}
    registry: dict[str, dict] = {}
    required = ("id", "claim", "sources", "applies_to", "checked_on", "status",
                "review_state", "consequence", "consumers")
    for i, c in enumerate(claims):
        cw = f"{where}: claims[{i}]"
        if not isinstance(c, dict):
            rep.error(cw, "claim must be an object")
            continue
        cid = c.get("id")
        if isinstance(cid, str):
            cw = f"{where}: {cid}"
        for k in required:
            if k not in c or c[k] in ("", None, []):
                rep.error(cw, f"missing '{k}'")
        unknown = set(c) - set(required) - {"research_ids", "notes"}
        if unknown:
            rep.error(cw, f"unknown fields {sorted(unknown)}")
        if not isinstance(cid, str) or not CLAIM_ID_RE.match(cid):
            rep.error(cw, "id must match C-AREA-NNN")
            continue
        if cid in registry:
            rep.error(cw, "duplicate claim id")
            continue
        registry[cid] = c
        if c.get("status") not in CLAIM_STATUS:
            rep.error(cw, f"status must be one of {CLAIM_STATUS}")
        if c.get("review_state") not in REVIEW_STATES:
            rep.error(cw, f"review_state must be one of {REVIEW_STATES}")
        d = parse_date(c.get("checked_on"), cw, "checked_on", rep)
        if d:
            if d > today:
                rep.error(cw, "checked_on is in the future")
            elif (today - d).days > max_age_days and c.get("review_state") == "current":
                rep.error(cw, f"stale: checked {d.isoformat()}, more than {max_age_days} days ago; "
                              "re-inspect the source or mark review-required")
        srcs = c.get("sources")
        if isinstance(srcs, list):
            for j, s in enumerate(srcs):
                sw = f"{cw} sources[{j}]"
                if not isinstance(s, dict):
                    rep.error(sw, "source must be an object")
                    continue
                if not s.get("locator"):
                    rep.error(sw, "missing 'locator' (path/function/section)")
                live = s.get("live") is True
                if "live" in s and not isinstance(s.get("live"), bool):
                    rep.error(sw, "'live' must be boolean")
                check_url(s.get("url"), sw, rep, live)
                pin = s.get("pin")
                if pin is not None and pin not in pins:
                    rep.error(sw, f"unknown pin '{pin}'")
                if pin is None and not live:
                    rep.error(sw, "source needs 'pin' or live=true")
                if pin in pins and isinstance(s.get("url"), str) and pins[pin].get("commit", "") not in s["url"] \
                        and "/releases/tag/" not in s["url"]:
                    rep.error(sw, f"url does not contain commit of pin '{pin}'")
        cons = c.get("consumers")
        if isinstance(cons, list):
            for con in cons:
                if not isinstance(con, str):
                    rep.error(cw, "consumer must be a string path")
                    continue
                cpath = Path(os.path.normpath(skills_dir / con))
                if not is_within(cpath, skills_dir) or not cpath.is_file():
                    rep.error(cw, f"consumer '{con}' is not a file inside the bundle")
                    continue
                t = read_text(cpath, rep, con) or ""
                if cid not in CLAIM_CITE_RE.findall(t):
                    rep.error(cw, f"consumer '{con}' does not cite {cid}")
    # Every citation in the bundle resolves; deprecated/unsupported claims are not cited.
    for p in sorted(skills_dir.rglob("*.md")):
        t = read_text(p, Report(), "") or ""
        for cid in sorted(set(CLAIM_CITE_RE.findall(t))):
            pw = rel(p, base)
            if cid not in registry:
                rep.error(pw, f"cites unknown claim {cid}")
                continue
            state = registry[cid].get("review_state")
            consumer = rel(p, skills_dir)
            if consumer not in registry[cid].get("consumers", []):
                rep.error(pw, f"cites {cid} but is not listed in its consumers")
            if state in ("deprecated", "unsupported"):
                rep.error(pw, f"cites {cid} whose review_state is '{state}'")
            elif state == "review-required":
                rep.warn(pw, f"cites {cid} which is review-required")
    return registry


# ---------------------------------------------------------------------------
# compatibility.json
# ---------------------------------------------------------------------------

def check_compatibility(root: Path, rep: Report, today: _dt.date) -> None:
    path = root / "compatibility.json"
    if not path.is_file():
        rep.error("compatibility.json", "missing")
        return
    data = load_json(path, rep, root)
    if not isinstance(data, dict):
        return
    w = "compatibility.json"
    if data.get("schema_version") != 1:
        rep.error(w, "schema_version must be 1")
    base = data.get("baseline")
    if not isinstance(base, dict):
        rep.error(w, "'baseline' must be an object")
        return
    fw = base.get("firmware", {})
    if not SHA40_RE.match(str(fw.get("commit", ""))):
        rep.error(w, "baseline.firmware.commit must be a full 40-hex commit")
    for comp in ("firmware", "target", "api", "sdk_artifact", "ufbt_bootstrap", "toolchain"):
        if comp not in base:
            rep.error(w, f"baseline.{comp} missing (record 'unknown' explicitly instead of omitting)")
            continue
        entry = base[comp]
        if not isinstance(entry, dict):
            rep.error(w, f"baseline.{comp} must be an object")
            continue
        if entry.get("status") not in CLAIM_STATUS:
            rep.error(w, f"baseline.{comp}.status must be one of {CLAIM_STATUS}")
        d = parse_date(entry.get("checked_on"), f"{w}: baseline.{comp}", "checked_on", rep)
        if d and d > today:
            rep.error(w, f"baseline.{comp}.checked_on is in the future")
        if entry.get("status") == "unresolved" and not entry.get("consequence"):
            rep.error(w, f"baseline.{comp} is unresolved and needs a 'consequence'")
    sdk = base.get("sdk_artifact", {})
    if isinstance(sdk, dict):
        if not SHA256_RE.match(str(sdk.get("published_sha256", ""))):
            rep.error(w, "baseline.sdk_artifact.published_sha256 must be 64 lowercase hex")
        if sdk.get("bytes_verified") is True and not SHA256_RE.match(str(sdk.get("observed_sha256", ""))):
            rep.error(w, "bytes_verified=true requires observed_sha256")
        if sdk.get("bytes_verified") is True and sdk.get("observed_sha256") != sdk.get("published_sha256"):
            rep.error(w, "observed_sha256 differs from published_sha256")
    ev = data.get("evidence")
    statuses: dict[str, str] = {}
    if not isinstance(ev, dict):
        rep.error(w, "'evidence' must be an object")
    else:
        for field in EVIDENCE_FIELDS:
            item = ev.get(field)
            if not isinstance(item, dict):
                rep.error(w, f"evidence.{field} missing")
                continue
            st = item.get("status")
            if st not in CHECK_STATUS:
                rep.error(w, f"evidence.{field}.status must be one of {CHECK_STATUS}")
            if not item.get("reason"):
                rep.error(w, f"evidence.{field}.reason required")
            if st in ("pass", "fail") and not item.get("evidence"):
                rep.error(w, f"evidence.{field} with status {st} needs an evidence path")
            if isinstance(item.get("evidence"), str) and not item["evidence"].startswith("https://"):
                ep = Path(os.path.normpath(root / item["evidence"].split("#")[0]))
                if not is_within(ep, root) or not ep.exists():
                    rep.error(w, f"evidence.{field}.evidence path not found: {item['evidence']}")
            statuses[field] = st
        extra = set(ev) - set(EVIDENCE_FIELDS)
        if extra:
            rep.error(w, f"unknown evidence fields {sorted(extra)}")
    surfaces = data.get("surfaces")
    if not isinstance(surfaces, list) or not surfaces:
        rep.error(w, "'surfaces' must be a non-empty list")
        return
    for s in surfaces:
        if not isinstance(s, dict):
            rep.error(w, "surface must be an object")
            continue
        sid = s.get("id", "?")
        lvl = s.get("support")
        if lvl not in SUPPORT_LEVELS:
            rep.error(w, f"surface {sid}: support must be one of {SUPPORT_LEVELS}")
            continue
        for needed in SUPPORT_REQUIRES.get(lvl, ()):
            if statuses.get(needed) != "pass":
                rep.error(w, f"surface {sid}: support '{lvl}' claimed but evidence.{needed} is not 'pass'")
        if not s.get("notes"):
            rep.error(w, f"surface {sid}: notes required")


# ---------------------------------------------------------------------------
# Evaluation definitions
# ---------------------------------------------------------------------------

def check_evaluations(root: Path, rep: Report) -> None:
    ev = root / "evaluations"
    if not ev.is_dir():
        rep.error("evaluations", "missing")
        return
    act = load_json(ev / "activation.json", rep, root) if (ev / "activation.json").is_file() else None
    if act is None:
        rep.error("evaluations/activation.json", "missing or invalid")
    else:
        cases = act.get("cases", []) if isinstance(act, dict) else []
        kinds = {"positive": 0, "negative": 0, "ambiguous": 0}
        ids = set()
        for c in cases:
            cid = c.get("id") if isinstance(c, dict) else None
            if not cid or cid in ids:
                rep.error("evaluations/activation.json", f"missing or duplicate case id {cid!r}")
                continue
            ids.add(cid)
            k = c.get("kind")
            if k not in kinds:
                rep.error("evaluations/activation.json", f"{cid}: kind must be one of {list(kinds)}")
                continue
            kinds[k] += 1
            for key in ("expected", "acceptable", "forbidden"):
                vals = c.get(key)
                if not isinstance(vals, list) or any(v not in SKILLS for v in vals):
                    rep.error("evaluations/activation.json", f"{cid}: '{key}' must list FRED skill names")
            if k == "negative" and c.get("expected"):
                rep.error("evaluations/activation.json", f"{cid}: negative control must expect no skill")
            if not c.get("prompt"):
                rep.error("evaluations/activation.json", f"{cid}: prompt required")
        if kinds != {"positive": 8, "negative": 8, "ambiguous": 8}:
            rep.error("evaluations/activation.json", f"expected 8/8/8 cases, found {kinds}")
    rub = load_json(ev / "rubric.json", rep, root) if (ev / "rubric.json").is_file() else None
    if rub is None:
        rep.error("evaluations/rubric.json", "missing or invalid")
    else:
        cats = rub.get("categories", []) if isinstance(rub, dict) else []
        total = sum(c.get("weight", 0) for c in cats if isinstance(c, dict))
        if total != 100:
            rep.error("evaluations/rubric.json", f"category weights sum to {total}, not 100")
    tasks = sorted((ev / "tasks").glob("*.md")) if (ev / "tasks").is_dir() else []
    if len(tasks) != 7:
        rep.error("evaluations/tasks", f"expected 7 task definitions, found {len(tasks)}")
    # Each task oracle's assertion weights match the rubric category weights.
    rubric_w = {c.get("id"): c.get("weight") for c in (rub or {}).get("categories", [])
                if isinstance(c, dict)} if isinstance(rub, dict) else {}
    for t in tasks:
        op = ev / "oracles" / (t.stem + ".json")
        ow = rel(op, root)
        if not op.is_file():
            rep.error(ow, f"missing oracle for task {t.stem}")
            continue
        orc = load_json(op, rep, root)
        if not isinstance(orc, dict):
            continue
        sums: dict = {}
        for a in orc.get("assertions", []):
            sums[a.get("category")] = sums.get(a.get("category"), 0) + a.get("weight", 0)
        if rubric_w and sums != rubric_w:
            rep.error(ow, f"assertion weights by category {sums} do not match rubric {rubric_w}")
        if not orc.get("critical"):
            rep.error(ow, "critical failures must be declared")
    # Isolation: agent-facing fixtures must not carry oracle identifiers.
    fixtures = ev / "fixtures"
    oracle_markers = re.compile(r"T0\d-D\d|expected_findings|seeded defect|oracle", re.I)
    if fixtures.is_dir():
        for p in sorted(fixtures.rglob("*")):
            if p.is_file():
                t = read_text(p, rep, rel(p, root))
                if t and oracle_markers.search(t):
                    rep.error(rel(p, root), "agent-facing fixture contains oracle/seeded-defect markers")


# ---------------------------------------------------------------------------

def run(root: Path, skills_dir: Path, bundle_only: bool, today: _dt.date,
        max_age_days: int) -> Report:
    rep = Report()
    anchor_cache: dict = {}
    base = root if not bundle_only else skills_dir
    check_skills(skills_dir, base, rep, anchor_cache)
    check_sources(skills_dir, base, rep, today, max_age_days)
    if not bundle_only:
        check_compatibility(root, rep, today)
        check_evaluations(root, rep)
        for p in sorted(root.rglob("*.md")):
            parts = p.relative_to(root).parts
            if parts[0] in (".git",) or is_within(p, skills_dir):
                continue
            t = read_text(p, rep, rel(p, root))
            if t is not None:
                check_links(p, t, root, root, rep, anchor_cache)
    return rep


def main(argv=None) -> int:
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(
        description="Offline integrity check for the FRED skill bundle. "
                    "Exit 0 = clean, 1 = errors, 2 = usage error.")
    ap.add_argument("--root", type=Path, default=here,
                    help="repository root (default: this script's repository)")
    ap.add_argument("--skills-dir", type=Path, default=None,
                    help="directory holding the four sibling skills "
                         "(default: ROOT/skills; may be an installed .claude/skills)")
    ap.add_argument("--bundle-only", action="store_true",
                    help="check only the skill payload (for an installed copy)")
    ap.add_argument("--today", default=None,
                    help="reference date YYYY-MM-DD for staleness checks (default: system date)")
    ap.add_argument("--max-age-days", type=int, default=120,
                    help="claims marked current must be checked within this many days (default 120)")
    ap.add_argument("--format", choices=("text", "json"), default="text")
    args = ap.parse_args(argv)
    try:
        today = _dt.date.fromisoformat(args.today) if args.today else _dt.date.today()
    except ValueError:
        ap.error("--today must be YYYY-MM-DD")
    root = Path(os.path.abspath(args.root))
    skills_dir = Path(os.path.abspath(args.skills_dir)) if args.skills_dir else root / "skills"
    if not args.bundle_only and not root.is_dir():
        print(f"error: root {root} is not a directory", file=sys.stderr)
        return 2
    rep = run(root, skills_dir, args.bundle_only, today, args.max_age_days)
    if args.format == "json":
        print(json.dumps({"errors": rep.errors, "warnings": rep.warnings,
                          "ok": not rep.errors}, indent=2, sort_keys=True))
    else:
        for w in rep.warnings:
            print(f"warning: {w}")
        for e in rep.errors:
            print(f"error: {e}")
        print(f"check_bundle: {len(rep.errors)} error(s), {len(rep.warnings)} warning(s)")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
