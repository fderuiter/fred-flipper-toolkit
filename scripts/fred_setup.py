#!/usr/bin/env python3
"""FRED setup wizard: guided install, health check and SDK setup.

Run with no arguments for a guided menu, or use a subcommand:

  install     copy the four FRED skills into a project or your personal skills folder
  doctor      check installations, Claude Code, uFBT/SDK and toolchain; suggest fixes
  sdk         set up the pinned official 1.5.1 f7 SDK in an isolated folder (downloads)
  build       build a FAP folder with the wizard-managed SDK (runs project build code)
  card        fill in a compatibility card for your Flipper project
  uninstall   remove the four FRED skills from a skills folder

Safety: every step that writes files, downloads or runs external programs is
shown first and needs your confirmation (or --yes). --dry-run shows the plan and
changes nothing. Nothing is installed automatically, nothing touches a Flipper
device, and no Claude Code setting, hook or permission is changed.

Python 3.8+ standard library only. On Windows use `py -3 scripts\\fred_setup.py`.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import check_bundle  # noqa: E402  (same directory, standard library only)

SKILLS = ("flipper-core", "flipper-native", "flipper-review", "flipper-verify")
SNIPPET_MARKER = "<!-- fred-routing -->"
SNIPPET = f"""
{SNIPPET_MARKER}
## Flipper Zero work
Flipper Zero development uses the FRED skills (flipper-core, flipper-native, flipper-review, flipper-verify).
Target and evidence: see docs/compatibility-card.md.
Do not deploy, flash, drive GPIO, transmit or write tags without explicit scope.
"""

# Pinned baseline (see compatibility.json); verified 2026-10-16.
SDK_URL = "https://update.flipperzero.one/builds/firmware/1.5.1/flipper-z-f7-sdk-1.5.1.zip"
SDK_SHA256 = "dfde39df32ba8a9789b26afc1d702579ad8399a1c285770637cfe3287b2e2ffe"
SDK_API_TABLE_SHA256 = "5c9505984a19be218c19400d43ab9c7056996d003e69809945d9db03e95c04bb"
SDK_API = "88.2"
TOOLCHAIN_VERSION = "39"
UFBT_REQUIREMENTS = """\
ufbt==0.2.6 \\
    --hash=sha256:ee3b5788b4034694aea594fd44777eab4049968d94c5f39ad93df8b05d8c8849 \\
    --hash=sha256:4f1a858858598ed2e25bbab69e2ea604bc00758c3b1e8ecf897a29866157363b
oslex==2.0.0 \\
    --hash=sha256:f1c2944072aafcb06b6ee8a6c018d9ebb8a7f43d7cf0b6630b4f5d7be45f6ae4 \\
    --hash=sha256:30d9f4a7201bdce3ab7d9cfc0f9ee9e18c423b2b1d1668141b0dd3594b368ffe
mslex==1.3.0 \\
    --hash=sha256:c7074b347201b3466fc077c5692fbce9b5f62a63a51f537a53fbbd02eff2eea4 \\
    --hash=sha256:641c887d1d3db610eee2af37a8e5abda3f70b3006cdfd2d0d29dc0d1ae28a85d
"""
DEFAULT_SDK_HOME = Path.home() / ".fred" / "sdk-1.5.1-f7"
MAX_DOWNLOAD = 200 * 1024 * 1024


class Abort(Exception):
    pass


class UI:
    """Small console helper; all prompts go through here so tests can drive it."""

    def __init__(self, yes: bool = False, dry_run: bool = False, interactive: bool | None = None):
        self.yes = yes
        self.dry_run = dry_run
        self.interactive = sys.stdin.isatty() if interactive is None else interactive

    def say(self, text: str = "") -> None:
        print(text)

    def step(self, text: str) -> None:
        print(f"\n== {text}")

    def ok(self, text: str) -> None:
        print(f"  [ok]   {text}")

    def warn(self, text: str) -> None:
        print(f"  [warn] {text}")

    def bad(self, text: str) -> None:
        print(f"  [fail] {text}")

    def hint(self, text: str) -> None:
        print(f"         -> {text}")

    def confirm(self, question: str, default: bool = False) -> bool:
        if self.dry_run:
            print(f"  (dry run) would ask: {question} — not doing it")
            return False
        if self.yes:
            print(f"  {question} yes (--yes)")
            return True
        if not self.interactive:
            raise Abort(f"needs confirmation: {question} (re-run with --yes, or run interactively)")
        suffix = " [Y/n] " if default else " [y/N] "
        answer = input(f"  {question}{suffix}").strip().lower()
        return default if not answer else answer in ("y", "yes")

    def ask(self, question: str, default: str = "") -> str:
        if self.yes or self.dry_run or not self.interactive:
            return default
        answer = input(f"  {question}" + (f" [{default}]: " if default else ": ")).strip()
        return answer or default

    def choose(self, question: str, options: list, default: int = 0) -> int:
        if self.yes or self.dry_run or not self.interactive:
            return default
        print(f"  {question}")
        for i, (label, _) in enumerate(options, 1):
            print(f"    {i}. {label}")
        while True:
            raw = input(f"  Choice [{default + 1}]: ").strip()
            if not raw:
                return default
            if raw.isdigit() and 1 <= int(raw) <= len(options):
                return int(raw) - 1
            print("  Please enter one of the numbers above.")


# ---------------------------------------------------------------------------
# Locations and inspection
# ---------------------------------------------------------------------------

def personal_skills_dir() -> Path:
    cfg = os.environ.get("CLAUDE_CONFIG_DIR")
    return (Path(cfg) if cfg else Path.home() / ".claude") / "skills"


def project_skills_dir(project: Path) -> Path:
    return project / ".claude" / "skills"


def tree_digest(path: Path) -> dict:
    out = {}
    for p in sorted(path.rglob("*")):
        if p.is_file() and not p.is_symlink():
            out[p.relative_to(path).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


def skill_version(skill_dir: Path) -> str:
    md = skill_dir / "SKILL.md"
    if not md.is_file():
        return "missing"
    fields, _, _ = check_bundle.parse_frontmatter(md.read_text(encoding="utf-8"))
    return fields.get("metadata", {}).get("version", "unknown")


def source_commit() -> str:
    head = ROOT / ".git" / "HEAD"
    try:
        ref = head.read_text().strip()
        if ref.startswith("ref: "):
            return (ROOT / ".git" / ref[5:]).read_text().strip()[:12]
        return ref[:12]
    except OSError:
        return "unknown"


def check_installed(target: Path) -> check_bundle.Report:
    today = _dt.date.today()
    return check_bundle.run(target, target, True, today, 10**6)


def describe_install(target: Path) -> dict:
    present = [s for s in SKILLS if (target / s / "SKILL.md").is_file()]
    info = {"target": str(target), "present": present,
            "versions": sorted({skill_version(target / s) for s in present})}
    meta = target / ".fred-install.json"
    if meta.is_file():
        try:
            info["record"] = json.loads(meta.read_text())
        except ValueError:
            info["record"] = "unreadable"
    return info


# ---------------------------------------------------------------------------
# install / uninstall
# ---------------------------------------------------------------------------

def plan_install(source: Path, target: Path) -> list:
    plan = []
    for name in SKILLS:
        src, dst = source / name, target / name
        if dst.is_symlink():
            raise Abort(f"{dst} is a symlink; refusing to write through it")
        if not dst.exists():
            plan.append((name, "new", []))
            continue
        a, b = tree_digest(src), tree_digest(dst)
        if a == b:
            plan.append((name, "same", []))
        else:
            changes = sorted({k for k in a.keys() | b.keys() if a.get(k) != b.get(k)})
            plan.append((name, "update", changes))
    return plan


def cmd_install(args, ui: UI) -> int:
    source = Path(args.source).resolve() if args.source else ROOT / "skills"
    ui.step("Checking the FRED bundle you are installing from")
    rep = check_installed(source)
    if rep.errors:
        for e in rep.errors:
            ui.bad(e)
        raise Abort("the source bundle has errors; fix them (python3 scripts/check_bundle.py) before installing")
    ui.ok(f"bundle at {source} is valid (version {skill_version(source / 'flipper-core')}, commit {source_commit()})")

    if args.target:
        target, scope = Path(args.target).expanduser().resolve(), "custom"
    else:
        scope = args.scope
        if not scope:
            idx = ui.choose("Where should FRED be installed?", [
                ("This project only (<project>/.claude/skills) — shared with collaborators via git", "project"),
                (f"Personal, all projects ({personal_skills_dir()})", "personal")], 0)
            scope = ("project", "personal")[idx]
        if scope == "project":
            project = Path(args.project or ui.ask("Project folder", os.getcwd())).expanduser().resolve()
            if not project.is_dir():
                raise Abort(f"project folder {project} does not exist")
            target = project_skills_dir(project)
        else:
            target = personal_skills_dir()
    if target == (ROOT / "skills").resolve():
        raise Abort("the target is this repository's own skills/ folder; choose another location")

    ui.step(f"Plan for {target}")
    plan = plan_install(source, target)
    for name, kind, changes in plan:
        if kind == "new":
            ui.say(f"  + {name}: install")
        elif kind == "same":
            ui.say(f"  = {name}: already installed and identical")
        else:
            ui.say(f"  ~ {name}: differs in {len(changes)} file(s): {', '.join(changes[:6])}{' …' if len(changes) > 6 else ''}")
    others = [p.name for p in target.glob("flipper-*") if p.is_dir() and p.name not in SKILLS] if target.is_dir() else []
    if others:
        ui.warn(f"other flipper-* folders present and left untouched: {', '.join(others)}")
    todo = [p for p in plan if p[1] != "same"]
    if not todo:
        ui.ok("nothing to do: FRED is already installed and up to date here")
    else:
        updates = [p for p in todo if p[1] == "update"]
        if updates:
            ui.say("  Existing copies will be backed up before they are replaced.")
        if not ui.confirm(f"Install {len(todo)} skill folder(s) into {target}?", default=True):
            ui.say("  Nothing changed.")
            return 0
        target.mkdir(parents=True, exist_ok=True)
        stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        backup = target.parent / "fred-backups" / stamp
        for name, kind, _ in todo:
            dst = target / name
            if kind == "update":
                backup.mkdir(parents=True, exist_ok=True)
                shutil.move(str(dst), str(backup / name))
            shutil.copytree(source / name, dst, symlinks=False)
            ui.ok(f"{name} {'updated' if kind == 'update' else 'installed'}")
        if (backup).exists():
            ui.ok(f"previous copies saved in {backup}")
        (target / ".fred-install.json").write_text(json.dumps({
            "installed": _dt.datetime.now().isoformat(timespec="seconds"),
            "version": skill_version(source / "flipper-core"), "commit": source_commit(),
            "source": str(source)}, indent=2) + "\n")

    ui.step("Verifying the installed copy")
    rep = check_installed(target)
    for e in rep.errors:
        ui.bad(e)
    if rep.errors:
        return 1
    ui.ok("all four skills present; shared references resolve; metadata valid")

    if scope == "project" and not args.no_snippet:
        add_snippet(target.parent.parent, ui)

    ui.step("Next steps")
    ui.say("  1. Restart Claude Code (or start a new session) in your project.")
    ui.say("  2. Ask something like: \"Can a stock Flipper control my BLE kettle?\" or \"Add a settings screen to this FAP\".")
    ui.say("  3. Optional, for real builds: python3 scripts/fred_setup.py sdk")
    ui.say("  4. Anytime: python3 scripts/fred_setup.py doctor")
    return 0


def add_snippet(project: Path, ui: UI) -> None:
    ui.step("Optional: routing note in CLAUDE.md")
    claude_md = project / "CLAUDE.md"
    existing = claude_md.read_text(encoding="utf-8") if claude_md.is_file() else ""
    if SNIPPET_MARKER in existing:
        ui.ok("CLAUDE.md already contains the FRED note")
        return
    ui.say("  This short note helps Claude route Flipper work to FRED; your existing text is kept:")
    for line in SNIPPET.strip().splitlines()[1:]:
        ui.say(f"    | {line}")
    if ui.confirm(f"Append it to {claude_md}?", default=False):
        with open(claude_md, "a", encoding="utf-8") as f:
            f.write(("\n" if existing and not existing.endswith("\n") else "") + SNIPPET)
        ui.ok("note appended")
    else:
        ui.say("  Skipped (FRED works without it).")


def cmd_uninstall(args, ui: UI) -> int:
    if args.target:
        target = Path(args.target).expanduser().resolve()
    elif args.scope == "personal":
        target = personal_skills_dir()
    else:
        target = project_skills_dir(Path(args.project or os.getcwd()).resolve())
    present = [s for s in SKILLS if (target / s).is_dir() and not (target / s).is_symlink()]
    if not present:
        ui.ok(f"no FRED skills found in {target}")
        return 0
    ui.step(f"Remove from {target}: {', '.join(present)}")
    ui.say("  Only these four folders (and .fred-install.json) are removed; a backup copy is kept.")
    if not ui.confirm("Remove them?", default=False):
        ui.say("  Nothing changed.")
        return 0
    backup = target.parent / "fred-backups" / (_dt.datetime.now().strftime("%Y%m%d-%H%M%S") + "-uninstall")
    backup.mkdir(parents=True, exist_ok=True)
    for name in present:
        shutil.move(str(target / name), str(backup / name))
    meta = target / ".fred-install.json"
    if meta.is_file():
        meta.unlink()
    ui.ok(f"removed; backup in {backup}")
    return 0


# ---------------------------------------------------------------------------
# doctor
# ---------------------------------------------------------------------------

def run_quiet(cmd, env=None, timeout=60):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).strip()
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, str(exc)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sdk_status(home: Path) -> dict:
    ufbt_home = home / "ufbt-home"
    st = {"home": str(home), "ufbt": None, "sdk_api": None, "api_table_ok": None, "toolchain": None}
    exe = venv_bin(home / "venv", "ufbt")
    if exe.exists():
        st["ufbt"] = str(exe)
    table = ufbt_home / "current" / "sdk_headers" / "f7_sdk" / "targets" / "f7" / "api_symbols.csv"
    if table.is_file():
        for line in table.read_text(encoding="utf-8").splitlines()[:3]:
            if line.startswith("Version,"):
                st["sdk_api"] = line.split(",")[2]
        st["api_table_ok"] = sha256_file(table) == SDK_API_TABLE_SHA256
    tc = ufbt_home / "toolchain" / "current" / "VERSION"
    if tc.is_file():
        st["toolchain"] = tc.read_text().strip()
    return st


def cmd_doctor(args, ui: UI) -> int:
    problems = 0
    ui.step("Python")
    if sys.version_info >= (3, 8):
        ui.ok(f"Python {platform.python_version()} on {platform.system()} {platform.machine()}")
    else:
        ui.bad(f"Python {platform.python_version()} is too old; 3.8+ needed")
        problems += 1

    ui.step("FRED installations")
    project = Path(args.project or os.getcwd()).resolve()
    found = 0
    for label, target in (("project", project_skills_dir(project)), ("personal", personal_skills_dir())):
        info = describe_install(target)
        if not info["present"]:
            ui.say(f"  - {label}: not installed ({target})")
            continue
        found += 1
        rep = check_installed(target)
        if len(info["present"]) < 4 or rep.errors:
            problems += 1
            ui.bad(f"{label}: broken installation at {target}")
            for e in rep.errors[:5]:
                ui.hint(e)
            ui.hint("re-run: python3 scripts/fred_setup.py install --scope " + label)
        elif len(info["versions"]) > 1:
            problems += 1
            ui.bad(f"{label}: mixed versions {info['versions']} — reinstall all four together")
        else:
            rec = info.get("record") or {}
            ui.ok(f"{label}: all four skills, version {info['versions'][0]}"
                  + (f", commit {rec.get('commit')}" if isinstance(rec, dict) and rec.get("commit") else "")
                  + f" ({target})")
            src = ROOT / "skills"
            if src.is_dir() and any(k != "same" for _, k, _ in plan_install(src, target)):
                ui.warn(f"{label} copy differs from this checkout")
                ui.hint(f"update: python3 scripts/fred_setup.py install --scope {label}")
    if not found:
        ui.warn("FRED is not installed in this project or your personal skills folder")
        ui.hint("install: python3 scripts/fred_setup.py install")

    ui.step("Claude Code")
    claude = shutil.which("claude")
    if claude:
        rc, out = run_quiet([claude, "--version"])
        ui.ok(f"claude found: {out.splitlines()[0] if out else claude}")
    else:
        ui.warn("the claude command was not found on PATH (fine if you use another client)")
        ui.hint("see https://code.claude.com/docs for installation")

    ui.step("Flipper build tools (optional)")
    home = Path(args.sdk_home).expanduser() if args.sdk_home else DEFAULT_SDK_HOME
    st = sdk_status(home)
    if st["ufbt"]:
        ui.ok(f"wizard-managed uFBT: {st['ufbt']}")
    elif shutil.which("ufbt"):
        ui.warn(f"ufbt found on PATH ({shutil.which('ufbt')}) but not a wizard-managed pinned copy")
        ui.hint("its SDK may come from the moving release channel; pin it or run the sdk wizard")
    else:
        ui.say(f"  - no uFBT set up (only needed to build apps): python3 scripts/fred_setup.py sdk")
    if st["sdk_api"]:
        if st["api_table_ok"]:
            ui.ok(f"SDK API {st['sdk_api']} — matches the pinned 1.5.1 table")
        else:
            problems += 1
            ui.bad(f"SDK API {st['sdk_api']} in {home} does not match the pinned 1.5.1 table")
            ui.hint("re-run: python3 scripts/fred_setup.py sdk")
    if st["toolchain"]:
        if st["toolchain"] == TOOLCHAIN_VERSION:
            ui.ok(f"toolchain version {st['toolchain']}")
        else:
            ui.warn(f"toolchain version {st['toolchain']}, expected {TOOLCHAIN_VERSION} for SDK 1.5.1")
    env_home = os.environ.get("UFBT_HOME")
    if env_home:
        ui.say(f"  - UFBT_HOME is set to {env_home}")

    ui.step("Summary")
    if problems:
        ui.bad(f"{problems} problem(s) found — see the hints above")
        return 1
    ui.ok("no problems found")
    return 0


# ---------------------------------------------------------------------------
# SDK wizard
# ---------------------------------------------------------------------------

def venv_bin(venv: Path, name: str) -> Path:
    if os.name == "nt":
        return venv / "Scripts" / (name + ".exe")
    return venv / "bin" / name


def download(url: str, dest: Path, ui: UI) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url, timeout=60) as r, open(tmp, "wb") as f:
        total = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            total += len(chunk)
            if total > MAX_DOWNLOAD:
                raise Abort("download larger than expected; stopped")
            f.write(chunk)
    tmp.replace(dest)
    ui.ok(f"downloaded {total:,} bytes")


def verify_sha256(path: Path, expected: str, ui: UI) -> None:
    actual = sha256_file(path)
    if actual != expected:
        raise Abort(f"SHA-256 mismatch for {path.name}: got {actual}, expected {expected}. "
                    "The file was NOT used. Delete it and try again; if it repeats, do not proceed.")
    ui.ok(f"SHA-256 verified ({expected[:16]}…)")


def cmd_sdk(args, ui: UI) -> int:
    home = Path(args.home).expanduser().resolve() if args.home else DEFAULT_SDK_HOME
    venv, ufbt_home = home / "venv", home / "ufbt-home"
    zip_path = Path(args.sdk_zip).resolve() if args.sdk_zip else home / "downloads" / "flipper-z-f7-sdk-1.5.1.zip"
    ui.step("Official Flipper SDK setup (firmware 1.5.1, target f7, API 88.2)")
    ui.say(f"""  Everything goes into one folder you can delete later: {home}
  Steps and their effects:
    1. Create a Python virtual environment and install uFBT 0.2.6 from PyPI
       (hash-pinned: ufbt, oslex, mslex).                         [downloads, installs into {venv}]
    2. Download the 1.5.1 f7 SDK and verify its SHA-256.          [downloads ~20 MB]
    3. Point uFBT at that verified SDK (isolated UFBT_HOME).      [writes {ufbt_home}]
    4. Optional: build a tiny test app. On first build uFBT downloads the
       official toolchain (version 39, ~350 MB) and runs SDK build scripts.
  Nothing is flashed or sent to a Flipper.""")
    if args.dry_run:
        py = venv_bin(venv, "python")
        ui.say(f"""
  Commands this would run:
    {sys.executable} -m venv {venv}
    {py} -m pip install --require-hashes -r <pinned requirements>
    download {SDK_URL} -> {zip_path} ; verify sha256 {SDK_SHA256}
    UFBT_HOME={ufbt_home} {venv_bin(venv, 'ufbt')} update --local={zip_path} --hw-target=f7""")
        return 0
    if not ui.confirm("Continue with step 1 (install uFBT into a private virtual environment)?", default=True):
        return 0
    if not venv_bin(venv, "python").exists():
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    req = home / "ufbt-requirements.txt"
    req.write_text(UFBT_REQUIREMENTS)
    subprocess.run([str(venv_bin(venv, "python")), "-m", "pip", "install", "--quiet", "--require-hashes",
                    "-r", str(req)], check=True)
    ui.ok("uFBT 0.2.6 installed with verified hashes")

    if zip_path.is_file():
        ui.say(f"  Using existing SDK archive {zip_path}")
    else:
        if not ui.confirm(f"Step 2: download the SDK from {SDK_URL}?", default=True):
            return 0
        download(SDK_URL, zip_path, ui)
    verify_sha256(zip_path, SDK_SHA256, ui)

    if not ui.confirm("Step 3: install this verified SDK into the isolated UFBT_HOME?", default=True):
        return 0
    env = dict(os.environ, UFBT_HOME=str(ufbt_home))
    subprocess.run([str(venv_bin(venv, "ufbt")), "update", f"--local={zip_path}", "--hw-target=f7"],
                   check=True, env=env)
    st = sdk_status(home)
    if st["api_table_ok"]:
        ui.ok(f"SDK installed: API {st['sdk_api']} matches the pinned table")
    else:
        raise Abort("the installed SDK's API table does not match the pinned 1.5.1 table")
    write_activate_scripts(home, ui)

    if ui.confirm("Step 4 (optional): build a small test app now? (downloads the toolchain on first use)",
                  default=False):
        sample = ROOT / "evaluations" / "reference" / "01-utility"
        rc = build_app(sample, home, ui, scratch=True)
        if rc:
            return rc
    ui.step("Done")
    ui.say(f"  Build any app folder:   python3 scripts/fred_setup.py build <app-folder>")
    ui.say(f"  Or use uFBT directly:   source {home / 'activate.sh'}   (PowerShell: . {home / 'activate.ps1'})")
    return 0


def write_activate_scripts(home: Path, ui: UI) -> None:
    venv, ufbt_home = home / "venv", home / "ufbt-home"
    (home / "activate.sh").write_text(
        f'# FRED pinned uFBT environment (source this file)\nexport UFBT_HOME="{ufbt_home}"\n'
        f'export PATH="{venv_bin(venv, "ufbt").parent}:$PATH"\n')
    (home / "activate.ps1").write_text(
        f'# FRED pinned uFBT environment (dot-source this file)\n$env:UFBT_HOME = "{ufbt_home}"\n'
        f'$env:PATH = "{venv_bin(venv, "ufbt").parent}" + [IO.Path]::PathSeparator + $env:PATH\n')
    ui.ok(f"wrote {home / 'activate.sh'} and activate.ps1")


def build_app(app: Path, home: Path, ui: UI, scratch: bool = False) -> int:
    fam = app / "application.fam"
    if not fam.is_file():
        raise Abort(f"{app} has no application.fam")
    exe = venv_bin(home / "venv", "ufbt")
    if not exe.exists():
        raise Abort("uFBT is not set up yet; run: python3 scripts/fred_setup.py sdk")
    ui.step(f"Build {app}")
    ui.say("  application.fam is Python that the build executes. Review it (and any build hooks) first:")
    for line in fam.read_text(encoding="utf-8").splitlines()[:25]:
        ui.say(f"    | {line}")
    if not scratch and not ui.confirm("Build this app with the pinned SDK?", default=True):
        return 0
    work = Path(tempfile.mkdtemp(prefix="fred-build-")) / app.name if scratch else app
    if scratch:
        shutil.copytree(app, work)
    env = dict(os.environ, UFBT_HOME=str(home / "ufbt-home"))
    p = subprocess.run([str(exe)], cwd=work, env=env, capture_output=True, text=True)
    log = [ln for ln in (p.stdout + p.stderr).strip().splitlines() if "###" not in ln and "%" not in ln[-8:]]
    for line in log[-12:]:
        ui.say(f"    {line}")
    faps = sorted((work / "dist").glob("*.fap"))
    if p.returncode != 0 or not faps:
        ui.bad("build failed — see the log above")
        return 1
    for fap in faps:
        ui.ok(f"{fap.name}  sha256 {sha256_file(fap)}")
    ui.say("  This proves the app compiles against SDK 1.5.1; it does not prove it works on a device.")
    ui.say("  `ufbt launch` would deploy to a connected Flipper — only do that when you intend to.")
    return 0


def cmd_build(args, ui: UI) -> int:
    home = Path(args.sdk_home).expanduser().resolve() if args.sdk_home else DEFAULT_SDK_HOME
    return build_app(Path(args.app).resolve(), home, ui)


# ---------------------------------------------------------------------------
# Compatibility card
# ---------------------------------------------------------------------------

CARD_FIELDS = [
    ("outcome", "What should the app or project do?", ""),
    ("firmware", "Firmware lineage / version on the device (unknown is fine)", "official 1.5.1 (assumed, unverified)"),
    ("target", "Hardware target", "f7"),
    ("surface", "Execution surface (FAP / JS / built-in / firmware / host / MCU / undecided)", "undecided"),
    ("sdk", "SDK API and archive", f"1.5.1 f7, API {SDK_API}, SHA-256 {SDK_SHA256[:16]}…"),
    ("hardware", "Hardware available for testing", "none"),
    ("actions", "Authorized actions (read / build / deploy / flash / physical output)", "read, build"),
]


def cmd_card(args, ui: UI) -> int:
    project = Path(args.project or os.getcwd()).resolve()
    out = project / "docs" / "compatibility-card.md"
    ui.step(f"Compatibility card for {project}")
    detected = {}
    fam = next(iter(sorted(project.glob("**/application.fam"))), None)
    if fam:
        detected["outcome"] = f"existing app in {fam.parent.relative_to(project) or '.'}"
        ui.ok(f"found {fam.relative_to(project)}")
    sdk_home = Path(args.sdk_home).expanduser() if getattr(args, "sdk_home", None) else DEFAULT_SDK_HOME
    st = sdk_status(sdk_home)
    if st["api_table_ok"]:
        detected["sdk"] = f"1.5.1 f7, API {st['sdk_api']} (verified, {sdk_home})"
    values = {}
    for key, question, default in CARD_FIELDS:
        values[key] = ui.ask(question, detected.get(key, default)) or "unknown"
    body = f"""# Compatibility card

Filled with `fred_setup.py card` on {_dt.date.today().isoformat()}. `unknown` is allowed; write what it blocks.
FRED's rules for this card: see the installed `flipper-core/references/compatibility.md`.

| Field | Value | Consequence if unknown |
|---|---|---|
| User outcome | {values['outcome']} | |
| Firmware lineage / version / commit | {values['firmware']} | Cannot claim imports resolve on the device |
| Hardware target / board revision | {values['target']} | |
| Execution surface | {values['surface']} | Decide with flipper-core before building |
| SDK API and archive | {values['sdk']} | Build evidence not possible |
| Toolchain | official toolchain {TOOLCHAIN_VERSION} when built with the FRED SDK wizard | |
| Hardware available | {values['hardware']} | Device behavior stays not-run |
| Authorized actions | {values['actions']} | |

## Capability decision

| Required operation | Header | Implementation | Export / library | Owner | Status |
|---|---|---|---|---|---|
| | | | | | |
"""
    ui.say("")
    for line in body.splitlines()[5:14]:
        ui.say(f"    {line}")
    if out.exists() and not ui.confirm(f"{out} exists. Replace it?", default=False):
        return 0
    if not out.exists() and not ui.confirm(f"Write {out}?", default=True):
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(body, encoding="utf-8")
    ui.ok(f"wrote {out}")
    return 0


# ---------------------------------------------------------------------------

def guided(ui: UI) -> int:
    print(__doc__.split("\n\n")[0])
    print("\nWhat would you like to do?")
    actions = [("Install FRED into a project or for all my projects", "install"),
               ("Check my setup (doctor)", "doctor"),
               ("Set up the official Flipper SDK for building apps", "sdk"),
               ("Fill in a compatibility card for my project", "card"),
               ("Uninstall FRED", "uninstall")]
    idx = ui.choose("Choose an option", actions, 0)
    return main([actions[idx][1]])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--yes", action="store_true", help="answer yes to confirmations (non-interactive use)")
    ap.add_argument("--dry-run", action="store_true", help="show what would happen; change nothing")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("install", help="install the four skills")
    p.add_argument("--scope", choices=("project", "personal"))
    p.add_argument("--project", help="project folder for --scope project (default: current folder)")
    p.add_argument("--target", help="explicit skills folder (advanced)")
    p.add_argument("--source", help="skills folder to install from (default: this checkout)")
    p.add_argument("--no-snippet", action="store_true", help="do not offer the CLAUDE.md routing note")
    p = sub.add_parser("uninstall", help="remove the four skills")
    p.add_argument("--scope", choices=("project", "personal"), default="project")
    p.add_argument("--project")
    p.add_argument("--target")
    p = sub.add_parser("doctor", help="check your setup")
    p.add_argument("--project")
    p.add_argument("--sdk-home")
    p = sub.add_parser("sdk", help="set up the pinned official SDK (downloads)")
    p.add_argument("--home", help=f"folder for the SDK environment (default {DEFAULT_SDK_HOME})")
    p.add_argument("--sdk-zip", help="use an already downloaded SDK archive (still hash-checked)")
    p = sub.add_parser("build", help="build a FAP folder with the pinned SDK")
    p.add_argument("app")
    p.add_argument("--sdk-home")
    p = sub.add_parser("card", help="fill in a compatibility card")
    p.add_argument("--project")
    p.add_argument("--sdk-home")
    for sp in sub.choices.values():
        sp.add_argument("--yes", action="store_true", default=argparse.SUPPRESS)
        sp.add_argument("--dry-run", action="store_true", default=argparse.SUPPRESS)
    args = ap.parse_args(argv)
    ui = UI(yes=getattr(args, "yes", False), dry_run=getattr(args, "dry_run", False))
    try:
        if not args.cmd:
            return guided(ui)
        return {"install": cmd_install, "uninstall": cmd_uninstall, "doctor": cmd_doctor,
                "sdk": cmd_sdk, "build": cmd_build, "card": cmd_card}[args.cmd](args, ui)
    except Abort as exc:
        print(f"\nStopped: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"\nStopped: command failed ({exc.returncode}): {' '.join(map(str, exc.cmd))}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
