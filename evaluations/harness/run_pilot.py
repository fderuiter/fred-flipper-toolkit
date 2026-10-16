#!/usr/bin/env python3
"""Reviewer-side controlled pilot for FRED (issue #8).

Phase "run": 7 tasks x N repetitions x 2 arms (baseline, treatment) of fresh
headless Claude Code sessions. Both arms get the same model, effort, tools,
permissions, budget, uniform prompt and a fresh copy of only
evaluations/fixtures/<task>/. Treatment additionally gets the four FRED skills
at project scope. Order is randomized with a recorded seed.

Phase "grade": each session's final answer and produced files are scored by a
separate grader session that sees the task definition and reviewer-side oracle
but not the arm label (session IDs are opaque). Scores are per predeclared
assertion; critical failures override points.

Phase "report": aggregates per task/arm, paired differences with a bootstrap
confidence interval over pairs, and gate status.

--set heldout runs the seven held-out variants from evaluations/heldout/
(fixtures, tasks, oracles) through the same arms, settings and grader. Never
tune skill text on held-out results (issue #12).

This tool launches model sessions and should only be run by an operator who
has authorized that spend. It installs nothing.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import random
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EV = ROOT / "evaluations"
SETS = {
    "training": EV,
    "heldout": EV / "heldout",
}
TASKS = ["01-utility", "02-settings", "03-uart", "04-ir-parser", "05-ble-feasibility",
         "06-lifecycle-review", "07-sdk-migration"]
FRED = ("flipper-core", "flipper-native", "flipper-review", "flipper-verify")
STRIP_ENV = ("CLAUDE_CODE_SESSION_ID", "CLAUDE_CODE_ADDITIONAL_DIRECTORIES_CLAUDE_MD",
             "CLAUDE_ADDITIONAL_DIRECTORIES", "CLAUDE_AFTER_LAST_COMPACT")
PROMPT = "Read PROJECT.md in the current directory and complete the request it describes."
TOOLS = "Read,Glob,Grep,Write,Edit,Skill,Bash"
ALLOWED = ["Read", "Glob", "Grep", "Write", "Edit", "Skill",
           "Bash(gcc *)", "Bash(cc *)", "Bash(clang *)", "Bash(make *)", "Bash(./*)", "Bash(ls *)",
           "Bash(python3 -c *)", "Bash(xxd *)", "Bash(od *)"]
PRIVATE = (str(ROOT), "/root", "/tmp/claude-0", "/home", "/tmp/fred-sdk", "/tmp/fred-build")


def set_root(name: str) -> Path:
    return SETS[name]


def set_tasks(name: str) -> list:
    """Task ids of a set: one per tasks/*.md with a matching fixture folder."""
    if name == "training":
        return list(TASKS)
    root = set_root(name)
    return sorted(p.stem for p in (root / "tasks").glob("*.md") if (root / "fixtures" / p.stem).is_dir())


def sid(task: str, rep: int, arm: str, seed: int) -> str:
    return hashlib.sha256(f"{seed}:{task}:{rep}:{arm}".encode()).hexdigest()[:10]


def deny_settings() -> dict:
    deny = []
    for root in PRIVATE:
        for tool in ("Read", "Edit", "Write", "Glob", "Grep"):
            deny.append(f"{tool}(/{root}/**)")
    return {"permissions": {"deny": deny}}


def run_session(job: dict, args) -> dict:
    base = Path(args.work) / job["id"]
    if base.exists():
        shutil.rmtree(base)
    ws = base / "ws"
    shutil.copytree(set_root(args.set) / "fixtures" / job["task"], ws)
    if job["arm"] == "treatment":
        for name in FRED:
            shutil.copytree(ROOT / "skills" / name, ws / ".claude" / "skills" / name)
    settings = base / "settings.json"
    settings.write_text(json.dumps(deny_settings()))
    env = {k: v for k, v in os.environ.items() if k not in STRIP_ENV}
    cmd = ["claude", "-p", PROMPT, "--model", args.model, "--effort", args.effort,
           "--tools", TOOLS, "--allowedTools", *ALLOWED, "--settings", str(settings),
           "--strict-mcp-config", "--no-session-persistence", "--permission-prompts", "none",
           "--output-format", "stream-json", "--verbose", "--max-budget-usd", str(args.budget)]
    try:
        p = subprocess.run(cmd, cwd=ws, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                           text=True, timeout=args.timeout)
        raw, rc = p.stdout, p.returncode
    except subprocess.TimeoutExpired as exc:
        raw = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        rc = "timeout"
    events, texts, skills, private, outside, result = [], [], [], [], [], {}
    for line in raw.splitlines():
        try:
            e = json.loads(line)
        except ValueError:
            continue
        if e.get("type") == "assistant":
            for c in e["message"].get("content", []):
                if c.get("type") == "tool_use":
                    inp = c.get("input", {})
                    events.append({"tool": c["name"], "input": {k: (v if len(str(v)) < 400 else str(v)[:400] + "…")
                                                                 for k, v in inp.items()}})
                    if c["name"] == "Skill":
                        skills.append(str(inp.get("skill", "")).split(":")[-1])
                    for key in ("file_path", "path", "command"):
                        val = str(inp.get(key, ""))
                        if any(r in val for r in PRIVATE):
                            private.append(val)
                        for tok in val.split():
                            if tok.startswith("/") and not tok.startswith(str(ws)) and not tok.startswith(
                                    ("/usr/", "/bin/", "/dev/null", "/tmp/fred-eval/pilot/" + job["id"])):
                                outside.append(tok)
                elif c.get("type") == "text":
                    texts.append(c["text"])
        elif e.get("type") == "result":
            u = e.get("usage", {})
            result = {"subtype": e.get("subtype"), "is_error": e.get("is_error"),
                      "cost_usd": e.get("total_cost_usd"), "num_turns": e.get("num_turns"),
                      "duration_ms": e.get("duration_ms"),
                      "tokens": {k: u.get(k) for k in ("input_tokens", "output_tokens",
                                                       "cache_read_input_tokens", "cache_creation_input_tokens")},
                      "final": e.get("result")}
    out = Path(args.out) / "sessions" / job["id"]
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ws, out / "workspace", ignore=shutil.ignore_patterns(".claude"))
    rec = {**job, "rc": rc, "skills_invoked": sorted(set(skills)), "private_paths": sorted(set(private)),
           "outside_workspace_paths": sorted(set(outside)),
           "result": result, "tool_events": events, "assistant_text": texts}
    (out / "session.json").write_text(json.dumps(rec, indent=2) + "\n")
    print(f"{job['id']} {job['task']} rep{job['rep']} {job['arm']}: rc={rc} cost={result.get('cost_usd')} "
          f"skills={rec['skills_invoked']} private={len(private)}", flush=True)
    return rec


def workspace_text(ws: Path, limit: int = 60000) -> str:
    parts, total = [], 0
    for p in sorted(ws.rglob("*")):
        if not p.is_file() or p.stat().st_size > 200000:
            continue
        try:
            t = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        chunk = f"\n===== FILE {p.relative_to(ws)} =====\n{t}"
        if total + len(chunk) > limit:
            parts.append(f"\n===== FILE {p.relative_to(ws)} (omitted: size budget) =====\n")
            continue
        parts.append(chunk)
        total += len(chunk)
    return "".join(parts)


GRADER_SCHEMA = {
    "type": "object",
    "properties": {
        "assertions": {"type": "array", "items": {"type": "object", "properties": {
            "id": {"type": "string"}, "awarded": {"type": "number"}, "rationale": {"type": "string"}},
            "required": ["id", "awarded", "rationale"]}},
        "critical_failures": {"type": "array", "items": {"type": "string"}},
        "seeded_defects_found": {"type": "array", "items": {"type": "string"}},
        "decoys_flagged": {"type": "array", "items": {"type": "string"}},
        "major_errors": {"type": "array", "items": {"type": "string"}},
    },
    "required": ["assertions", "critical_failures", "major_errors"],
}


def grade_session(sdir: Path, args) -> dict:
    rec = json.loads((sdir / "session.json").read_text())
    task = rec["task"]
    root = set_root(args.set)
    oracle = (root / "oracles" / f"{task}.json").read_text()
    taskdef = (root / "tasks" / f"{task}.md").read_text()
    rubric = (EV / "rubric.json").read_text()
    fixture_files = sorted(str(p.relative_to(root / "fixtures" / task))
                           for p in (root / "fixtures" / task).rglob("*") if p.is_file())
    final = (rec.get("result") or {}).get("final") or "\n".join(rec.get("assistant_text", []))
    tools = json.dumps(rec.get("tool_events", [])[:200], indent=0)[:20000]
    prompt = f"""You are a strict, blinded reviewer scoring one AI coding-agent session for a Flipper Zero
benchmark. You do not know which configuration produced it; judge only the evidence below.

Score every assertion in the oracle from 0 up to its weight (partial credit allowed, integers or halves).
Use the oracle's expectations exactly; do not reward behaviour the oracle does not ask for. An assertion
about executed evidence (compile, tests) earns credit only if the session actually ran something or
honestly reported it as not run; claimed-but-unexecuted results are fabricated evidence (critical).
Record every critical failure from the oracle's and rubric's lists that occurred. For review tasks (06 and its
held-out variant) also list the seeded defect IDs correctly identified with a correct causal trace, and any decoys reported as defects.
List major errors (wrong technical claims, invented APIs, unsafe advice) in major_errors.

The workspace started with these files (task inputs): {fixture_files}

=== RUBRIC ===
{rubric}
=== TASK DEFINITION ===
{taskdef}
=== ORACLE ===
{oracle}
=== SESSION FINAL ANSWER ===
{final}
=== SESSION TOOL CALLS (truncated) ===
{tools}
=== SESSION WORKSPACE AFTER RUN ===
{workspace_text(sdir / 'workspace')}
"""
    env = {k: v for k, v in os.environ.items() if k not in STRIP_ENV}
    gdir = Path(args.work) / "grader"
    gdir.mkdir(parents=True, exist_ok=True)
    cmd = ["claude", "-p", "--model", args.grader_model, "--effort", "high", "--tools", "",
           "--strict-mcp-config", "--no-session-persistence", "--output-format", "json",
           "--json-schema", json.dumps(GRADER_SCHEMA), "--max-budget-usd", "3"]
    p = subprocess.run(cmd, input=prompt, cwd=gdir, env=env, capture_output=True, text=True, timeout=900)
    try:
        outer = json.loads(p.stdout)
        res = outer.get("structured_output") or json.loads(outer.get("result", "{}"))
        cost = outer.get("total_cost_usd")
    except (ValueError, TypeError):
        res, cost = {"error": p.stdout[-2000:] + p.stderr[-2000:]}, None
    weights = {a["id"]: a for a in json.loads(oracle)["assertions"]}
    total = 0.0
    for a in res.get("assertions", []):
        w = weights.get(a.get("id"), {}).get("weight", 0)
        a["awarded"] = max(0.0, min(float(a.get("awarded", 0)), float(w)))
        total += a["awarded"]
    res["score"] = round(total, 1)
    res["grader_model"] = args.grader_model
    res["grader_cost_usd"] = cost
    (sdir / "grade.json").write_text(json.dumps(res, indent=2) + "\n")
    print(f"graded {sdir.name}: {res.get('score')} critical={res.get('critical_failures')}", flush=True)
    return res


def bootstrap_ci(diffs: list, reps: int = 10000, seed: int = 0, level: float = 0.95):
    """Percentile bootstrap CI of the mean paired difference (resampling pairs)."""
    if len(diffs) < 2:
        return None
    rnd = random.Random(seed)
    n = len(diffs)
    means = sorted(sum(rnd.choice(diffs) for _ in range(n)) / n for _ in range(reps))
    lo = means[int((1 - level) / 2 * reps)]
    hi = means[min(reps - 1, int((1 + level) / 2 * reps))]
    return [round(lo, 1), round(hi, 1)]


def bootstrap_ci_clustered(by_task: dict, reps: int = 10000, seed: int = 0, level: float = 0.95):
    """Sensitivity check: resample tasks, then pairs within each drawn task, so
    task-to-task heterogeneity widens the interval."""
    tasks = [t for t, v in by_task.items() if v]
    if len(tasks) < 2:
        return None
    rnd = random.Random(seed)
    means = []
    for _ in range(reps):
        xs = []
        for _ in tasks:
            vals = by_task[rnd.choice(tasks)]
            xs.extend(rnd.choice(vals) for _ in vals)
        means.append(sum(xs) / len(xs))
    means.sort()
    return [round(means[int((1 - level) / 2 * reps)], 1),
            round(means[min(reps - 1, int((1 + level) / 2 * reps))], 1)]


def report(args) -> dict:
    out = Path(args.out)
    tasks = set_tasks(args.set)
    rows = []
    for sdir in sorted((out / "sessions").iterdir()):
        rec = json.loads((sdir / "session.json").read_text())
        g = json.loads((sdir / "grade.json").read_text()) if (sdir / "grade.json").exists() else {}
        rows.append({"id": rec["id"], "task": rec["task"], "rep": rec["rep"], "arm": rec["arm"],
                     "score": g.get("score"), "critical": g.get("critical_failures", []),
                     "major": len(g.get("major_errors", [])),
                     "seeded": g.get("seeded_defects_found"), "decoys": g.get("decoys_flagged"),
                     "cost": (rec.get("result") or {}).get("cost_usd"),
                     "tokens": (rec.get("result") or {}).get("tokens"),
                     "skills": rec.get("skills_invoked"), "private": rec.get("private_paths"),
                     "rc": rec.get("rc")})
    def mean(xs):
        xs = [x for x in xs if x is not None]
        return round(statistics.mean(xs), 1) if xs else None
    per_task = {}
    for t in tasks:
        b = [r for r in rows if r["task"] == t and r["arm"] == "baseline"]
        tr = [r for r in rows if r["task"] == t and r["arm"] == "treatment"]
        per_task[t] = {"baseline_mean": mean([r["score"] for r in b]),
                       "treatment_mean": mean([r["score"] for r in tr]),
                       "baseline_critical": sum(bool(r["critical"]) for r in b),
                       "treatment_critical": sum(bool(r["critical"]) for r in tr),
                       "baseline_major": sum(r["major"] for r in b),
                       "treatment_major": sum(r["major"] for r in tr)}
    pairs, pairs_by_task = [], {}
    for t in tasks:
        for rep in range(1, args.reps + 1):
            b = next((r for r in rows if r["task"] == t and r["rep"] == rep and r["arm"] == "baseline"), None)
            tr = next((r for r in rows if r["task"] == t and r["rep"] == rep and r["arm"] == "treatment"), None)
            if b and tr and b["score"] is not None and tr["score"] is not None:
                pairs.append(tr["score"] - b["score"])
                pairs_by_task.setdefault(t, []).append(tr["score"] - b["score"])
    def tokens(r):
        t = r.get("tokens") or {}
        return sum(v or 0 for v in t.values())
    tb = [tokens(r) for r in rows if r["arm"] == "baseline"]
    tt = [tokens(r) for r in rows if r["arm"] == "treatment"]
    t_mean = mean([r["score"] for r in rows if r["arm"] == "treatment"])
    b_mean = mean([r["score"] for r in rows if r["arm"] == "baseline"])
    crit_t = sum(bool(r["critical"]) for r in rows if r["arm"] == "treatment")
    cm_b = sum(bool(r["critical"]) + r["major"] for r in rows if r["arm"] == "baseline")
    cm_t = sum(bool(r["critical"]) + r["major"] for r in rows if r["arm"] == "treatment")
    summary = {
        "model": args.model, "grader_model": args.grader_model, "sessions": len(rows),
        "treatment_mean": t_mean, "baseline_mean": b_mean,
        "set": args.set,
        "mean_paired_improvement": mean(pairs), "pairs": len(pairs),
        "paired_improvement_ci95_bootstrap": bootstrap_ci(pairs, seed=args.seed),
        "paired_improvement_ci_method": "percentile bootstrap over task-repetition pairs, 10000 resamples",
        "paired_improvement_ci95_task_clustered": bootstrap_ci_clustered(pairs_by_task, seed=args.seed),
        "min_task_family_treatment_mean": min((v["treatment_mean"] for v in per_task.values()
                                               if v["treatment_mean"] is not None), default=None),
        "treatment_sessions_with_critical": crit_t,
        "critical_plus_major_baseline": cm_b, "critical_plus_major_treatment": cm_t,
        "median_tokens_baseline": statistics.median(tb) if tb else None,
        "median_tokens_treatment": statistics.median(tt) if tt else None,
        "total_session_cost_usd": round(sum(r["cost"] or 0 for r in rows), 2),
        "isolation_violations": [r["id"] for r in rows if r["private"]],
        "per_task": per_task, "rows": rows,
    }
    g = summary
    ratio = (g["median_tokens_treatment"] / g["median_tokens_baseline"]) if g["median_tokens_baseline"] else None
    g["token_ratio_median"] = round(ratio, 2) if ratio else None
    g["gates"] = {
        "treatment_mean_ge_85": t_mean is not None and t_mean >= 85,
        "no_task_family_below_80": g["min_task_family_treatment_mean"] is not None and g["min_task_family_treatment_mean"] >= 80,
        "no_critical_failures": crit_t == 0,
        "paired_improvement_ge_10": (g["mean_paired_improvement"] or 0) >= 10,
        "critical_major_reduction_ge_50pct": cm_b > 0 and cm_t <= 0.5 * cm_b,
        "token_ratio_le_1_5": ratio is not None and ratio <= 1.5,
    }
    # Rubric v2 (ADR 0003): the higher token budget applies only with a measured correctness gain.
    comp = json.loads((EV / "rubric.json").read_text())["gates"]["comparative"]
    gain = g["gates"]["critical_major_reduction_ge_50pct"] and (t_mean or 0) >= (b_mean or 0)
    budget = comp["token_ratio_median_max"] if gain else comp.get("token_ratio_median_max_without_condition",
                                                                    comp["token_ratio_median_max"])
    g["token_budget"] = budget
    g["gates"]["token_ratio_within_budget"] = ratio is not None and ratio <= budget
    g["gates"]["comparative_gate_met"] = (g["gates"]["treatment_mean_ge_85"] and g["gates"]["no_task_family_below_80"]
                                          and g["gates"]["no_critical_failures"]
                                          and (g["gates"]["paired_improvement_ge_10"]
                                               or g["gates"]["critical_major_reduction_ge_50pct"]))
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k != "rows"}, indent=2))
    return summary


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("phase", choices=("run", "grade", "report"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--model", default="claude-sonnet-5-5")
    ap.add_argument("--grader-model", default="claude-opus-5-5")
    ap.add_argument("--effort", default="medium")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--seed", type=int, default=20261016)
    ap.add_argument("--budget", type=float, default=3.0)
    ap.add_argument("--timeout", type=int, default=1500)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--work", default="/tmp/fred-eval/pilot")
    ap.add_argument("--only", default="", help="comma-separated session ids (rerun/grade subset)")
    ap.add_argument("--set", choices=tuple(SETS), default="training",
                    help="task set: training (evaluations/) or heldout (evaluations/heldout/)")
    args = ap.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if args.phase == "run":
        jobs = [{"task": t, "rep": r, "arm": a, "id": sid(t, r, a, args.seed)}
                for t in set_tasks(args.set) for r in range(1, args.reps + 1) for a in ("baseline", "treatment")]
        random.Random(args.seed).shuffle(jobs)
        if args.only:
            jobs = [j for j in jobs if j["id"] in args.only.split(",")]
        (out / "run.json").write_text(json.dumps({
            "set": args.set, "model": args.model, "effort": args.effort, "budget_usd_per_session": args.budget,
            "timeout_s": args.timeout, "seed": args.seed, "prompt": PROMPT, "tools": TOOLS,
            "allowed": ALLOWED, "deny_roots": PRIVATE, "order": jobs}, indent=2) + "\n")
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(lambda j: run_session(j, args), jobs))
    elif args.phase == "grade":
        sdirs = sorted((out / "sessions").iterdir())
        if args.only:
            sdirs = [s for s in sdirs if s.name in args.only.split(",")]
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(lambda s: grade_session(s, args), sdirs))
    else:
        report(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
