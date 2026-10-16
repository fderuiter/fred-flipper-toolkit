#!/usr/bin/env python3
"""Measure which FRED files pilot sessions loaded and estimate what each cost (issue #13).

Offline and read-only: parses retained pilot records
(evaluations/results/<round>/sessions/*/session.json), launches nothing.

Per treatment session it finds the skills invoked (Skill tool) and every
skill file read through Read or Bash. A loaded file stays in context for the
rest of the session and is re-read from the prompt cache on every later turn,
so its estimated cost is

    size_tokens x turns_remaining

where size_tokens is the current file size divided by CHARS_PER_TOKEN, and
turns_remaining = num_turns - estimated load turn + 1. The load turn is
estimated from the event's position among the session's tool events, because
records do not keep turn numbers. These are estimates, not measured token
counts; the measured per-session totals come from the result usage fields.

With --measured-at REV, file sizes are taken from git revision REV (the skill
revision the sessions actually ran) and the report adds a projection: each
treatment session's measured total minus (size at REV - current size) x
turns_remaining for every file it loaded, holding turns and behaviour fixed.
A projection is not a rerun.

Usage:
    python3 evaluations/harness/measure_reference_load.py ROUND_DIR [ROUND_DIR ...]
        [--measured-at REV] [--format md|json]
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKILLS = ROOT / "skills"
CHARS_PER_TOKEN = 4.0
FILE_RE = re.compile(r"\.claude/skills/((?:flipper-[a-z]+)/(?:[\w./-]+\.md))")
NAME_RE = re.compile(r"\b((?:references|assets)/[\w-]+\.md|SKILL\.md)\b")


def size_tokens(rel: str, rev: str | None = None) -> int:
    if rev:
        p = subprocess.run(["git", "-C", str(ROOT), "show", f"{rev}:skills/{rel}"],
                           capture_output=True, text=True)
        return math.ceil(len(p.stdout) / CHARS_PER_TOKEN) if p.returncode == 0 else 0
    p = SKILLS / rel
    return math.ceil(len(p.read_text(encoding="utf-8")) / CHARS_PER_TOKEN) if p.exists() else 0


def total_tokens(rec: dict) -> int:
    return sum(v or 0 for v in ((rec.get("result") or {}).get("tokens") or {}).values())


def loads(rec: dict) -> dict[str, int]:
    """Map skill-relative file path -> index of the first tool event that loaded it."""
    found: dict[str, int] = {}
    events = rec.get("tool_events", [])
    for i, e in enumerate(events):
        inp = e.get("input", {})
        if e.get("tool") == "Skill":
            name = str(inp.get("skill", "")).split(":")[-1]
            found.setdefault(f"{name}/SKILL.md", i)
        elif e.get("tool") == "Read":
            m = FILE_RE.search(str(inp.get("file_path", "")))
            if m:
                found.setdefault(m.group(1), i)
        elif e.get("tool") == "Bash":
            cmd = str(inp.get("command", ""))
            if ".claude/skills" not in cmd or not re.search(r"\b(cat|head|sed|less|more)\b", cmd):
                continue
            # Resolve bare names (cd into a skill dir, then cat references/x.md).
            cd = re.search(r"\.claude/skills/(flipper-[a-z]+)(?:/(references|assets))?", cmd)
            for m in FILE_RE.finditer(cmd):
                found.setdefault(m.group(1), i)
            if cd:
                for n in re.findall(r"\b([\w-]+\.md)\b", cmd):
                    for rel in (f"{cd.group(1)}/{cd.group(2)}/{n}" if cd.group(2) else None,
                                f"{cd.group(1)}/{n}", f"{cd.group(1)}/references/{n}",
                                f"{cd.group(1)}/assets/{n}"):
                        if rel and (SKILLS / rel).exists():
                            found.setdefault(rel, i)
                            break
    return found


def analyse(rounds: list[Path], rev: str | None = None) -> dict:
    sessions, per_file = [], defaultdict(lambda: {"loads": 0, "cost": []})
    for rnd in rounds:
        for f in sorted(rnd.glob("sessions/*/session.json")):
            rec = json.loads(f.read_text(encoding="utf-8"))
            res = rec.get("result") or {}
            turns = res.get("num_turns") or 1
            row = {"round": rnd.name, "task": rec["task"], "rep": rec["rep"], "arm": rec["arm"],
                   "turns": turns, "tokens": total_tokens(rec), "files": []}
            row["projected"] = row["tokens"]
            if rec["arm"] == "treatment":
                n_ev = max(len(rec.get("tool_events", [])), 1)
                for rel, idx in sorted(loads(rec).items()):
                    turn = min(turns, 1 + math.floor(idx / n_ev * turns))
                    cost = size_tokens(rel, rev) * (turns - turn + 1)
                    row["projected"] -= (size_tokens(rel, rev) - size_tokens(rel)) * (turns - turn + 1)
                    per_file[rel]["loads"] += 1
                    per_file[rel]["cost"].append(cost)
                    row["files"].append(rel)
            sessions.append(row)
    files = []
    for rel, v in per_file.items():
        files.append({"file": rel, "size_tokens": size_tokens(rel, rev), "current_tokens": size_tokens(rel),
                      "sessions_loading": v["loads"],
                      "median_cost": int(statistics.median(v["cost"])), "total_cost": sum(v["cost"])})
    files.sort(key=lambda r: -r["total_cost"])
    pairs = []
    by = {(s["round"], s["task"], s["rep"], s["arm"]): s for s in sessions}
    for (rnd, task, rep, arm), t in sorted(by.items()):
        if arm != "treatment" or (rnd, task, rep, "baseline") not in by:
            continue
        b = by[(rnd, task, rep, "baseline")]
        pairs.append({"round": rnd, "task": task, "rep": rep, "loaded": bool(t["files"]),
                      "ratio": round(t["tokens"] / b["tokens"], 2) if b["tokens"] else None,
                      "extra_turns": t["turns"] - b["turns"]})
    def med(arm, loaded=None, key="tokens"):
        xs = [s[key] for s in sessions if s["arm"] == arm
              and (loaded is None or bool(s["files"]) == loaded)]
        return statistics.median(xs) if xs else None
    groups = {}
    for label, sel in (("loaded", True), ("not loaded", False)):
        ps = [p for p in pairs if p["loaded"] == sel]
        groups[label] = {"pairs": len(ps),
                         "median_pair_ratio": round(statistics.median(p["ratio"] for p in ps), 2) if ps else None,
                         "median_extra_turns": statistics.median(p["extra_turns"] for p in ps) if ps else None}
    mb, mt, mp = med("baseline"), med("treatment"), med("treatment", key="projected")
    return {"rounds": [r.name for r in rounds], "chars_per_token": CHARS_PER_TOKEN, "measured_at": rev,
            "median_tokens_baseline": mb, "median_tokens_treatment": mt,
            "median_ratio": round(mt / mb, 3) if mb else None,
            "median_tokens_treatment_projected": mp,
            "median_ratio_projected": round(mp / mb, 3) if mb else None,
            "pair_groups": groups, "files": files, "pairs": pairs}


def markdown(a: dict) -> str:
    at = a["measured_at"] or "working tree"
    out = [f"Rounds: {', '.join(a['rounds'])}. Sizes at {at}, ≈ characters / {a['chars_per_token']:g}.", "",
           "| File | ≈ tokens | Now | Sessions loading | Median est. cost | Total est. cost |",
           "|---|---:|---:|---:|---:|---:|"]
    for r in a["files"]:
        out.append(f"| `{r['file']}` | {r['size_tokens']:,} | {r['current_tokens']:,} | {r['sessions_loading']} | "
                   f"{r['median_cost']:,} | {r['total_cost']:,} |")
    out += ["", "| Treatment session | Pairs | Median pair ratio | Median extra turns |", "|---|---:|---:|---:|"]
    for k, g in a["pair_groups"].items():
        out.append(f"| {k} skill files | {g['pairs']} | {g['median_pair_ratio']} | {g['median_extra_turns']} |")
    out += ["", f"Median tokens: baseline {a['median_tokens_baseline']:,.0f}, treatment {a['median_tokens_treatment']:,.0f} "
            f"(ratio {a['median_ratio']}). Projected treatment with current file sizes: "
            f"{a['median_tokens_treatment_projected']:,.0f} (ratio {a['median_ratio_projected']})."]
    return "\n".join(out) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rounds", nargs="+", type=Path)
    ap.add_argument("--measured-at", metavar="REV", help="git revision of the skills the sessions ran")
    ap.add_argument("--format", choices=("md", "json"), default="md")
    args = ap.parse_args(argv)
    missing = [r for r in args.rounds if not r.is_dir()]
    if missing:
        print(f"not a directory: {missing[0]}", file=sys.stderr)
        return 2
    a = analyse(args.rounds, args.measured_at)
    print(markdown(a) if args.format == "md" else json.dumps(a, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
