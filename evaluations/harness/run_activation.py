#!/usr/bin/env python3
"""Reviewer-side live activation harness for FRED (issues #3 and #8).

This is evaluation tooling, not one of the toolkit helpers: it launches the
Claude Code CLI (`claude -p`) once per case in a fresh workspace and records
which skills and shared references each session loaded. Run it only where the
operator has authorized model sessions. It never installs anything.

Isolation per case:
  * fresh workspace under --work (default /tmp/fred-eval/runs), outside the repo;
  * FRED skills installed at project scope (<ws>/.claude/skills) or personal
    scope (a fresh CLAUDE_CONFIG_DIR/skills, session started from an unrelated
    directory);
  * tools limited to Read, Glob, Grep, Skill (no shell, no writes);
  * deny rules for the repository and other private paths; no session
    persistence; external MCP servers disabled (--strict-mcp-config);
  * post-run audit flags any tool input path outside the workspace/skills.

Usage:
  python3 evaluations/harness/run_activation.py --model claude-sonnet-5-5 \
      --scope project --out evaluations/results/<run-id> [--ids P01,N03] [--jobs 6]
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FRED = ("flipper-core", "flipper-native", "flipper-review", "flipper-verify")
SHARED = ("engineering-rules.md", "evidence-policy.md")
STRIP_ENV = ("CLAUDE_CODE_SESSION_ID", "CLAUDE_CODE_ADDITIONAL_DIRECTORIES_CLAUDE_MD",
             "CLAUDE_ADDITIONAL_DIRECTORIES", "CLAUDE_AFTER_LAST_COMPACT")
DENY = {"permissions": {"deny": [
    f"Read(/{ROOT}/**)", f"Glob(/{ROOT}/**)", f"Grep(/{ROOT}/**)",
    "Read(//tmp/claude-0/**)", "Read(//root/**)", "Glob(//root/**)", "Grep(//root/**)",
]}}


def run_case(case: dict, args, run_dir: Path) -> dict:
    cid = case["id"]
    base = Path(args.work) / run_dir.name / cid
    if base.exists():
        shutil.rmtree(base)
    ws = base / "ws"
    ws.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if k not in STRIP_ENV}
    if args.scope == "project":
        skills_dir = ws / ".claude" / "skills"
    else:
        cfg = base / "config"
        skills_dir = cfg / "skills"
        env["CLAUDE_CONFIG_DIR"] = str(cfg)
    skills_dir.mkdir(parents=True)
    for name in FRED:
        shutil.copytree(ROOT / "skills" / name, skills_dir / name)
    deny_path = base / "deny.json"
    deny_path.write_text(json.dumps(DENY))
    cmd = ["claude", "-p", case["prompt"], "--model", args.model, "--effort", args.effort,
           "--tools", "Read,Glob,Grep,Skill", "--settings", str(deny_path),
           "--strict-mcp-config", "--no-session-persistence", "--permission-prompts", "none",
           "--output-format", "stream-json", "--verbose", "--max-budget-usd", str(args.budget)]
    started = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    try:
        proc = subprocess.run(cmd, cwd=ws, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=args.timeout)
        raw, rc = proc.stdout, proc.returncode
    except subprocess.TimeoutExpired as exc:
        raw, rc = (exc.stdout or b"").decode() if isinstance(exc.stdout, bytes) else (exc.stdout or ""), "timeout"
    return analyse(case, raw, rc, started, ws, skills_dir, run_dir)


def analyse(case, raw, rc, started, ws, skills_dir, run_dir):
    events, invoked, reads, other_skills, outside, texts = [], [], [], [], [], []
    init, result = {}, {}
    allowed_roots = (str(ws), str(skills_dir))
    private_roots = (str(ROOT), "/root", "/tmp/claude-0", "/home")
    private = []
    for line in raw.splitlines():
        try:
            e = json.loads(line)
        except ValueError:
            continue
        if e.get("type") == "system" and e.get("subtype") == "init":
            init = {"model": e.get("model"), "claude_code_version": e.get("claude_code_version"),
                    "skills_listed": len(e.get("skills", [])),
                    "fred_listed": [s for s in e.get("skills", []) if s in FRED],
                    "tools": e.get("tools"), "permissionMode": e.get("permissionMode")}
        elif e.get("type") == "assistant":
            for c in e["message"].get("content", []):
                if c.get("type") == "tool_use":
                    inp = c.get("input", {})
                    events.append({"tool": c["name"], "input": inp})
                    if c["name"] == "Skill":
                        name = str(inp.get("skill", "")).split(":")[-1]
                        (invoked if name in FRED else other_skills).append(name)
                    for key in ("file_path", "path", "pattern"):
                        val = inp.get(key)
                        if isinstance(val, str) and val.startswith("/"):
                            if c["name"] == "Read":
                                reads.append(val)
                            if not val.startswith(allowed_roots):
                                outside.append(val)
                            if val.startswith(private_roots):
                                private.append(val)
                elif c.get("type") == "text":
                    texts.append(c["text"])
        elif e.get("type") == "result":
            result = {"subtype": e.get("subtype"), "is_error": e.get("is_error"),
                      "cost_usd": e.get("total_cost_usd"), "num_turns": e.get("num_turns"),
                      "usage": {k: e.get("usage", {}).get(k) for k in
                                ("input_tokens", "output_tokens", "cache_read_input_tokens",
                                 "cache_creation_input_tokens")},
                      "text": e.get("result")}
    for r in reads:
        m = re.search(r"/(flipper-[a-z]+)/SKILL\.md$", r)
        if m and m.group(1) in FRED:
            invoked.append(m.group(1))
    loaded = sorted(set(invoked))
    expected, acceptable, forbidden = set(case["expected"]), set(case["acceptable"]), set(case["forbidden"])
    shared_read = {s: any(r.endswith("/flipper-core/references/" + s) for r in reads) for s in SHARED}
    refs = sorted({r.split("/skills/")[-1] for r in reads if "/flipper-core/" in r and not r.endswith("SKILL.md")})
    reasons = []
    if not expected <= set(loaded):
        reasons.append(f"missing expected {sorted(expected - set(loaded))}")
    if set(loaded) & forbidden:
        reasons.append(f"loaded forbidden {sorted(set(loaded) & forbidden)}")
    extra = set(loaded) - expected - acceptable
    if extra:
        reasons.append(f"loaded outside expected+acceptable {sorted(extra)}")
    if set(loaded) == set(FRED):
        reasons.append("loaded all four skills")
    if result.get("is_error") or rc not in (0,):
        reasons.append(f"session error rc={rc} subtype={result.get('subtype')}")
    non_core = [s for s in loaded if s != "flipper-core"]
    direct_entry = None
    if non_core:
        direct_entry = {"non_core_loaded": non_core, "core_loaded": "flipper-core" in loaded,
                        "shared_rules_read": shared_read,
                        "pass": all(shared_read.values())}
    record = {"id": case["id"], "kind": case["kind"], "prompt": case["prompt"], "started": started,
              "expected": sorted(expected), "acceptable": sorted(acceptable), "forbidden": sorted(forbidden),
              "loaded_fred": loaded, "loaded_other_skills": sorted(set(other_skills)),
              "references_read": refs, "direct_entry": direct_entry,
              "outside_workspace_paths": sorted(set(outside)), "private_paths": sorted(set(private)),
              "isolation_ok": not private,
              "pass": not reasons, "reasons": reasons, "init": init, "result": result,
              "tool_events": events, "assistant_text": texts}
    (run_dir / "sessions").mkdir(exist_ok=True)
    (run_dir / "sessions" / f"{case['id']}.json").write_text(json.dumps(record, indent=2) + "\n")
    return record


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--effort", default="medium")
    ap.add_argument("--scope", choices=("project", "personal"), default="project")
    ap.add_argument("--cases", default=str(ROOT / "evaluations" / "activation.json"))
    ap.add_argument("--ids", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--work", default="/tmp/fred-eval/runs")
    ap.add_argument("--budget", type=float, default=1.5)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args(argv)
    cases = json.loads(Path(args.cases).read_text())["cases"]
    if args.ids:
        wanted = set(args.ids.split(","))
        cases = [c for c in cases if c["id"] in wanted]
    run_dir = Path(args.out)
    run_dir.mkdir(parents=True, exist_ok=True)
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        records = list(pool.map(lambda c: run_case(c, args, run_dir), cases))
    records.sort(key=lambda r: r["id"])
    neg = [r for r in records if r["kind"] == "negative"]
    summary = {
        "run": run_dir.name, "model": args.model, "effort": args.effort, "scope": args.scope,
        "claude_code_version": next((r["init"].get("claude_code_version") for r in records if r["init"]), None),
        "skills_listed": next((r["init"].get("skills_listed") for r in records if r["init"]), None),
        "cases": len(records), "passed": sum(r["pass"] for r in records),
        "negative_false_activations": sum(bool(r["loaded_fred"]) for r in neg),
        "direct_entry": [{"id": r["id"], **r["direct_entry"]} for r in records if r["direct_entry"]],
        "isolation_violations": [r["id"] for r in records if not r["isolation_ok"]],
        "total_cost_usd": round(sum((r["result"].get("cost_usd") or 0) for r in records), 4),
        "results": [{"id": r["id"], "kind": r["kind"], "pass": r["pass"], "loaded_fred": r["loaded_fred"],
                     "loaded_other_skills": r["loaded_other_skills"], "reasons": r["reasons"]} for r in records],
    }
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k != "results"}, indent=2))
    for r in summary["results"]:
        print(f"{r['id']:4} {'PASS' if r['pass'] else 'FAIL'} fred={r['loaded_fred']} other={r['loaded_other_skills']} {'; '.join(r['reasons'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
