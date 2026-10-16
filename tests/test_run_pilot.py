"""Offline tests for evaluations/harness/run_pilot.py: task sets, bootstrap CI and
report aggregation over synthetic session records. Launches no model sessions."""

import argparse
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "evaluations" / "harness"))
import run_pilot  # noqa: E402


class TaskSets(unittest.TestCase):
    def test_training_set(self):
        self.assertEqual(run_pilot.set_tasks("training"), run_pilot.TASKS)

    def test_heldout_set_has_seven_variants_with_fixtures_and_oracles(self):
        tasks = run_pilot.set_tasks("heldout")
        self.assertEqual(len(tasks), 7)
        root = run_pilot.set_root("heldout")
        for t in tasks:
            self.assertTrue((root / "fixtures" / t / "PROJECT.md").is_file(), t)
            oracle = json.loads((root / "oracles" / f"{t}.json").read_text(encoding="utf-8"))
            self.assertEqual(oracle["task"], t)
        self.assertFalse(set(tasks) & set(run_pilot.TASKS))


class Bootstrap(unittest.TestCase):
    def test_needs_two_pairs(self):
        self.assertIsNone(run_pilot.bootstrap_ci([]))
        self.assertIsNone(run_pilot.bootstrap_ci([3.0]))

    def test_constant_differences_give_degenerate_interval(self):
        self.assertEqual(run_pilot.bootstrap_ci([5.0] * 6, seed=1), [5.0, 5.0])

    def test_interval_brackets_mean_and_is_deterministic(self):
        diffs = [9.5, 10.0, -2.0, 20.0, 5.0, 12.0, 8.0, 0.0, 15.0]
        lo, hi = run_pilot.bootstrap_ci(diffs, seed=7)
        mean = sum(diffs) / len(diffs)
        self.assertLess(lo, mean)
        self.assertGreater(hi, mean)
        self.assertEqual(run_pilot.bootstrap_ci(diffs, seed=7), [lo, hi])


class Report(unittest.TestCase):
    def test_heldout_report_from_synthetic_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            tasks = run_pilot.set_tasks("heldout")
            scores = {"baseline": 70.0, "treatment": 85.0}
            for t in tasks:
                for rep in (1, 2):
                    for arm in ("baseline", "treatment"):
                        sid = run_pilot.sid(t, rep, arm, 1)
                        d = out / "sessions" / sid
                        d.mkdir(parents=True)
                        rec = {"id": sid, "task": t, "rep": rep, "arm": arm, "rc": 0,
                               "skills_invoked": [], "private_paths": [],
                               "result": {"cost_usd": 0.1, "tokens": {"input_tokens": 100}}}
                        (d / "session.json").write_text(json.dumps(rec))
                        bonus = rep if arm == "treatment" else 0
                        (d / "grade.json").write_text(json.dumps(
                            {"score": scores[arm] + bonus, "critical_failures": [], "major_errors": []}))
            args = argparse.Namespace(out=str(out), set="heldout", reps=2, seed=1,
                                      model="m", grader_model="g")
            with contextlib.redirect_stdout(io.StringIO()):
                summary = run_pilot.report(args)
            self.assertEqual(summary["set"], "heldout")
            self.assertEqual(summary["pairs"], 14)
            self.assertEqual(sorted(summary["per_task"]), tasks)
            self.assertEqual(summary["mean_paired_improvement"], 16.5)
            lo, hi = summary["paired_improvement_ci95_bootstrap"]
            self.assertTrue(15.0 <= lo <= 16.5 <= hi <= 17.0)
            self.assertTrue(summary["gates"]["paired_improvement_ge_10"])
            clo, chi = summary["paired_improvement_ci95_task_clustered"]
            self.assertTrue(clo <= 16.5 <= chi)


if __name__ == "__main__":
    unittest.main()
