#!/usr/bin/env python3
"""Compare two Flipper Zero SDK API tables (api_symbols.csv) offline.

Usage:
    python3 scripts/compare_api.py --old OLD.csv --new NEW.csv [--format text|json]
                                   [--fail-on removed,changed,major]

Input format: CSV with header ``entry,status,name,type,params``. Entries are
``Version`` (name = "MAJOR.MINOR"), ``Header`` (name = header path),
``Function`` (type = return type, params = parameter list) and ``Variable``
(type = variable type). Status ``+`` means exported to FAPs, ``-`` means
listed but not exported.

Reported changes:
  * API version, and whether the major number changed;
  * removed exports (row absent from NEW, or present but no longer ``+``);
  * added exports;
  * changed exports: same entry/name exported in both, but type or params differ.

Limits: a missing row does not prove that a macro, static inline function or
private-library symbol is absent or unavailable; this tool does not read
headers, link ELF files or prove source compatibility. The official build and
loader remain authoritative.

Exit status:
  0  no selected regressions
  1  selected regressions found (default selection: removed,changed,major)
  2  usage error, unreadable file, or malformed/ambiguous table
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from pathlib import Path

HEADER = ["entry", "status", "name", "type", "params"]
ENTRIES = ("Version", "Header", "Function", "Variable")
MAX_BYTES = 16 * 1024 * 1024
VERSION_RE = re.compile(r"^(\d+)\.(\d+)$")
FAIL_CHOICES = ("removed", "changed", "major", "added", "minor", "none")
LIMITS_NOTE = ("Absent rows do not prove absence of macros, inline functions or "
               "private-library symbols; official build/loader output is authoritative.")


class TableError(Exception):
    pass


def norm(text: str) -> str:
    """Normalise whitespace only; types/params are otherwise compared verbatim."""
    text = re.sub(r"\s+", " ", text.strip())
    text = re.sub(r"\s*,\s*", ", ", text)
    text = re.sub(r"\s*\*", "*", text)
    return text


def load_table(path: Path) -> dict:
    try:
        size = path.stat().st_size
    except OSError as exc:
        raise TableError(f"{path}: cannot read ({exc.strerror})")
    if size > MAX_BYTES:
        raise TableError(f"{path}: larger than {MAX_BYTES} bytes")
    try:
        raw = path.read_bytes().decode("utf-8")
    except UnicodeDecodeError:
        raise TableError(f"{path}: not valid UTF-8")
    if raw.startswith("﻿"):
        raw = raw[1:]
    reader = csv.reader(io.StringIO(raw, newline=""), strict=True)
    errors: list[str] = []
    warnings: list[str] = []
    version = None
    symbols: dict[tuple[str, str], dict] = {}
    try:
        header = next(reader, None)
        if header != HEADER:
            raise TableError(f"{path}: header must be {','.join(HEADER)}")
        for row in reader:
            line = reader.line_num
            if not row or all(not c.strip() for c in row):
                continue
            if len(row) != 5:
                errors.append(f"line {line}: expected 5 fields, found {len(row)}")
                continue
            entry, status, name, typ, params = (c.strip() for c in row)
            if entry not in ENTRIES:
                errors.append(f"line {line}: unknown entry kind '{entry}'")
                continue
            if status not in ("+", "-"):
                errors.append(f"line {line}: status must be '+' or '-', found '{status}'")
                continue
            if not name:
                errors.append(f"line {line}: empty name")
                continue
            if entry == "Version":
                if not VERSION_RE.match(name):
                    errors.append(f"line {line}: malformed version '{name}'")
                elif version is not None:
                    errors.append(f"line {line}: duplicate Version row")
                else:
                    version = name
                continue
            if entry in ("Function", "Variable") and not typ:
                errors.append(f"line {line}: {entry} '{name}' has empty type")
                continue
            key = (entry, name)
            rec = {"status": status, "type": norm(typ), "params": norm(params), "line": line}
            if key in symbols:
                prev = symbols[key]
                same = all(prev[k] == rec[k] for k in ("status", "type", "params"))
                if same:
                    warnings.append(f"line {line}: duplicate identical row for {entry} '{name}'")
                else:
                    errors.append(f"line {line}: conflicting duplicate for {entry} '{name}' "
                                  f"(first at line {prev['line']})")
                continue
            symbols[key] = rec
    except csv.Error as exc:
        raise TableError(f"{path}: CSV syntax error near line {reader.line_num}: {exc}")
    if version is None:
        errors.append("no Version row")
    if errors:
        shown = "; ".join(errors[:20])
        more = f" (+{len(errors) - 20} more)" if len(errors) > 20 else ""
        raise TableError(f"{path}: malformed table: {shown}{more}")
    return {"version": version, "symbols": symbols, "warnings": warnings}


def compare(old: dict, new: dict) -> dict:
    o_major, o_minor = (int(x) for x in old["version"].split("."))
    n_major, n_minor = (int(x) for x in new["version"].split("."))
    removed, added, changed = [], [], []
    keys = sorted(set(old["symbols"]) | set(new["symbols"]))
    for key in keys:
        entry, name = key
        o = old["symbols"].get(key)
        n = new["symbols"].get(key)
        o_exp = bool(o and o["status"] == "+")
        n_exp = bool(n and n["status"] == "+")
        if o_exp and not n_exp:
            removed.append({"entry": entry, "name": name,
                            "reason": "row absent" if n is None else "no longer exported",
                            "old": sig(o)})
        elif n_exp and not o_exp:
            added.append({"entry": entry, "name": name,
                          "reason": "new row" if o is None else "newly exported",
                          "new": sig(n)})
        elif o_exp and n_exp and (o["type"] != n["type"] or o["params"] != n["params"]):
            changed.append({"entry": entry, "name": name, "old": sig(o), "new": sig(n)})
    return {
        "version": {
            "old": old["version"], "new": new["version"],
            "major_changed": o_major != n_major,
            "minor_changed": o_minor != n_minor,
            "direction": ("upgrade" if (n_major, n_minor) > (o_major, o_minor)
                          else "downgrade" if (n_major, n_minor) < (o_major, o_minor) else "same"),
        },
        "removed": removed,
        "added": added,
        "changed": changed,
        "warnings": {"old": old["warnings"], "new": new["warnings"]},
        "limits": LIMITS_NOTE,
    }


def sig(rec: dict) -> dict:
    return {"type": rec["type"], "params": rec["params"]}


def regressions(result: dict, fail_on: set[str]) -> list[str]:
    found = []
    if "removed" in fail_on and result["removed"]:
        found.append("removed")
    if "changed" in fail_on and result["changed"]:
        found.append("changed")
    if "major" in fail_on and result["version"]["major_changed"]:
        found.append("major")
    if "minor" in fail_on and result["version"]["minor_changed"]:
        found.append("minor")
    if "added" in fail_on and result["added"]:
        found.append("added")
    return found


def render_text(result: dict, regress: list[str]) -> str:
    v = result["version"]
    out = [f"API version: {v['old']} -> {v['new']} ({v['direction']}"
           f"{', MAJOR CHANGED' if v['major_changed'] else ''})"]
    out.append(f"removed exports: {len(result['removed'])}; changed: {len(result['changed'])}; "
               f"added: {len(result['added'])}")
    for r in result["removed"]:
        out.append(f"  - {r['entry']} {r['name']} ({r['reason']})")
    for c in result["changed"]:
        out.append(f"  ~ {c['entry']} {c['name']}: {c['old']['type']}({c['old']['params']})"
                   f" -> {c['new']['type']}({c['new']['params']})")
    for a in result["added"]:
        out.append(f"  + {a['entry']} {a['name']} ({a['reason']})")
    for side in ("old", "new"):
        for w in result["warnings"][side]:
            out.append(f"warning ({side}): {w}")
    out.append(f"note: {result['limits']}")
    out.append("result: " + ("REGRESSIONS: " + ", ".join(regress) if regress else "no selected regressions"))
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Compare two explicit SDK api_symbols.csv files. Exit 0 = no selected "
                    "regressions, 1 = regressions, 2 = usage or malformed input.",
        epilog=LIMITS_NOTE)
    ap.add_argument("--old", required=True, type=Path, help="baseline api_symbols.csv")
    ap.add_argument("--new", required=True, type=Path, help="candidate api_symbols.csv")
    ap.add_argument("--format", choices=("text", "json"), default="text")
    ap.add_argument("--fail-on", default="removed,changed,major",
                    help="comma list from: removed, changed, major, minor, added, none "
                         "(default: removed,changed,major)")
    args = ap.parse_args(argv)
    fail_on = {x.strip() for x in args.fail_on.split(",") if x.strip()}
    bad = fail_on - set(FAIL_CHOICES)
    if bad or not fail_on:
        print(f"error: invalid --fail-on value(s): {', '.join(sorted(bad)) or '(empty)'}", file=sys.stderr)
        return 2
    if "none" in fail_on:
        fail_on = set()
    try:
        old = load_table(args.old)
        new = load_table(args.new)
    except TableError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    result = compare(old, new)
    regress = regressions(result, fail_on)
    result["regressions"] = regress
    if args.format == "json":
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(render_text(result, regress))
    return 1 if regress else 0


if __name__ == "__main__":
    sys.exit(main())
