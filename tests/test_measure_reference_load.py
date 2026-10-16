import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "evaluations" / "harness"))

import measure_reference_load as m  # noqa: E402


def record(arm, turns, tokens, events):
    return {"task": "01-utility", "rep": 1, "arm": arm, "id": arm,
            "result": {"num_turns": turns, "tokens": {"input_tokens": tokens}},
            "tool_events": events}


class MeasureReferenceLoad(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.round = Path(self.tmp.name) / "round"
        ws = "/tmp/x/ws/.claude/skills"
        self.write("b", record("baseline", 4, 1000, [{"tool": "Read", "input": {"file_path": "/tmp/x/ws/PROJECT.md"}}]))
        self.write("t", record("treatment", 4, 2000, [
            {"tool": "Skill", "input": {"skill": "flipper-native"}},
            {"tool": "Bash", "input": {"command": f"cd {ws}/flipper-core; cat references/engineering-rules.md "
                                                  "references/evidence-policy.md"}},
            {"tool": "Read", "input": {"file_path": f"{ws}/flipper-core/references/runtime-ui.md"}},
            {"tool": "Write", "input": {"file_path": "/tmp/x/ws/app.c"}},
        ]))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, sid, rec):
        d = self.round / "sessions" / sid
        d.mkdir(parents=True)
        (d / "session.json").write_text(json.dumps(rec))

    def test_detects_skill_read_and_bash_loads(self):
        rec = json.loads((self.round / "sessions/t/session.json").read_text())
        self.assertEqual(m.loads(rec), {
            "flipper-native/SKILL.md": 0,
            "flipper-core/references/engineering-rules.md": 1,
            "flipper-core/references/evidence-policy.md": 1,
            "flipper-core/references/runtime-ui.md": 2,
        })

    def test_costs_and_pair_ratio(self):
        a = m.analyse([self.round])
        self.assertEqual(a["median_ratio"], 2.0)
        self.assertEqual(a["median_ratio_projected"], 2.0)  # no revision: sizes unchanged
        self.assertEqual(a["pair_groups"]["loaded"], {"pairs": 1, "median_pair_ratio": 2.0, "median_extra_turns": 0})
        rules = next(f for f in a["files"] if f["file"].endswith("engineering-rules.md"))
        # Loaded at event 1 of 4 in a 4-turn session -> turn 2 -> 3 turns in context.
        self.assertEqual(rules["total_cost"], 3 * m.size_tokens("flipper-core/references/engineering-rules.md"))

    def test_missing_round_dir(self):
        self.assertEqual(m.main([str(Path(self.tmp.name) / "nope")]), 2)


if __name__ == "__main__":
    unittest.main()
