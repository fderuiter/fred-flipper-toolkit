"""Tests for scripts/check_bundle.py (standard-library unittest, offline)."""

import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import check_bundle  # noqa: E402

TODAY = "2026-10-16"
SKILLS = ("flipper-core", "flipper-native", "flipper-review", "flipper-verify")


def run(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = check_bundle.main([str(a) for a in argv])
    return code, out.getvalue()


class RepositoryPasses(unittest.TestCase):
    def test_repository_is_clean(self):
        code, out = run("--root", ROOT, "--today", TODAY)
        self.assertEqual(code, 0, out)

    def test_json_output(self):
        code, out = run("--root", ROOT, "--today", TODAY, "--format", "json")
        data = json.loads(out)
        self.assertTrue(data["ok"])
        self.assertEqual(data["errors"], [])

    def test_budgets_hold(self):
        total = 0
        for name in SKILLS:
            text = (ROOT / "skills" / name / "SKILL.md").read_text(encoding="utf-8")
            self.assertLessEqual(text.rstrip("\n").count("\n") + 1, 150)
            self.assertLessEqual(len(text.split()), 1000)
            fields, _, errs = check_bundle.parse_frontmatter(text)
            self.assertEqual(errs, [])
            total += len(fields["description"].split())
        self.assertLessEqual(total, 300)
        rules = (ROOT / "skills/flipper-core/references/engineering-rules.md").read_text(encoding="utf-8")
        self.assertLessEqual(len(rules.split()), 1200)


class BundleFixture(unittest.TestCase):
    """Copies skills/ into a temporary directory that can be mutated."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.skills = self.base / "skills"
        shutil.copytree(ROOT / "skills", self.skills)

    def tearDown(self):
        self.tmp.cleanup()

    def check(self, *extra):
        return run("--bundle-only", "--skills-dir", self.skills, "--today", TODAY, *extra)

    def assert_error(self, fragment, *extra):
        code, out = self.check(*extra)
        self.assertEqual(code, 1, out)
        self.assertIn(fragment, out)

    def skill(self, name="flipper-native"):
        return self.skills / name / "SKILL.md"

    def edit(self, path, old, new, count=1):
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        path.write_text(text.replace(old, new, count), encoding="utf-8")


class Installation(BundleFixture):
    def test_copy_passes(self):
        code, out = self.check()
        self.assertEqual(code, 0, out)

    def test_project_and_personal_scope_from_unrelated_cwd(self):
        project = self.base / "project" / ".claude" / "skills"
        home = self.base / "home" / ".claude" / "skills"
        elsewhere = self.base / "unrelated"
        elsewhere.mkdir()
        for dest in (project, home):
            dest.parent.mkdir(parents=True)
            shutil.copytree(self.skills, dest)
        cwd = os.getcwd()
        try:
            os.chdir(elsewhere)
            for dest in (project, home):
                code, out = run("--bundle-only", "--skills-dir", dest, "--today", TODAY)
                self.assertEqual(code, 0, out)
        finally:
            os.chdir(cwd)

    def test_missing_core_sibling(self):
        shutil.rmtree(self.skills / "flipper-core")
        self.assert_error("broken installation: sibling skill 'flipper-core' is missing")

    def test_missing_shared_rules_file(self):
        (self.skills / "flipper-core/references/engineering-rules.md").unlink()
        code, out = self.check()
        self.assertEqual(code, 1)
        self.assertIn("required shared file is missing", out)
        self.assertIn("broken link '../flipper-core/references/engineering-rules.md'", out)

    def test_non_core_must_link_shared_rules(self):
        self.edit(self.skill("flipper-review"), "[../flipper-core/references/evidence-policy.md](../flipper-core/references/evidence-policy.md)", "the evidence policy")
        self.assert_error("must explicitly link shared reference '../flipper-core/references/evidence-policy.md'")

    def test_non_core_must_state_install_error(self):
        self.edit(self.skill("flipper-verify"), "Broken installation", "Problem")
        self.assert_error("must state the missing-shared-file installation error")

    def test_required_section(self):
        self.edit(self.skill(), "## Failure modes", "## Pitfalls")
        self.assert_error("missing required section '## Failure modes'")

    def test_unexpected_sibling_is_warning(self):
        (self.skills / "flipper-extra").mkdir()
        code, out = self.check()
        self.assertEqual(code, 0, out)
        self.assertIn("unexpected flipper-* directory", out)


class Frontmatter(BundleFixture):
    def fm(self, old, new):
        self.edit(self.skill(), old, new)

    def test_duplicate_key(self):
        self.fm("license: MIT\n", "license: MIT\nlicense: Apache-2.0\n")
        self.assert_error("duplicate key 'license'")

    def test_unknown_key(self):
        self.fm("license: MIT\n", "license: MIT\nversion: one\n")
        self.assert_error("unknown key 'version'")

    def test_allowed_tools_forbidden(self):
        self.fm("license: MIT\n", "license: MIT\nallowed-tools: Bash\n")
        self.assert_error("key 'allowed-tools' is forbidden")

    def test_quoted_value_rejected(self):
        self.fm("license: MIT", 'license: "MIT"')
        self.assert_error("starts with an ambiguous character")

    def test_block_scalar_rejected(self):
        self.fm("license: MIT", "license: |")
        self.assert_error("starts with an ambiguous character")

    def test_colon_space_rejected(self):
        self.fm("license: MIT", "license: MIT: or not")
        self.assert_error("contains ': '")

    def test_yaml_keyword_and_number_rejected(self):
        self.fm("license: MIT", "license: yes")
        self.assert_error("is a YAML keyword")

    def test_numeric_rejected(self):
        self.fm("  version: 0.1.0-preview", "  version: 0.1")
        self.assert_error("looks numeric")

    def test_tab_rejected(self):
        self.fm("license: MIT", "license:\tMIT")
        self.assert_error("tab characters are not allowed")

    def test_comment_and_blank_rejected(self):
        self.fm("license: MIT\n", "license: MIT\n# note\n\n")
        code, out = self.check()
        self.assertIn("comments are not allowed", out)
        self.assertIn("blank lines are not allowed", out)

    def test_unclosed_frontmatter(self):
        text = self.skill().read_text(encoding="utf-8").replace("\n---\n", "\n--\n", 1)
        self.skill().write_text(text, encoding="utf-8")
        self.assert_error("frontmatter is not closed")

    def test_missing_frontmatter(self):
        text = self.skill().read_text(encoding="utf-8")
        self.skill().write_text("# Title\n" + text, encoding="utf-8")
        self.assert_error("file must start with a '---'")

    def test_name_must_match_directory(self):
        self.fm("name: flipper-native", "name: flipper-nativ")
        self.assert_error("does not match directory")

    def test_name_charset_and_reserved(self):
        self.fm("name: flipper-native", "name: Flipper_Native")
        self.assert_error("invalid name")
        self.fm("name: Flipper_Native", "name: claude-native")
        self.assert_error("reserved word 'claude'")

    def test_description_angle_brackets(self):
        self.fm("description: Implement", "description: Implement <b>")
        self.assert_error("must not contain angle brackets")

    def test_dynamic_command_rejected(self):
        self.fm("license: MIT", "license: MIT !`date`")
        self.assert_error("dynamic pattern")
        sentinel = self.base / "sentinel"
        self.assertFalse(sentinel.exists())

    def test_dynamic_body_rejected_and_not_executed(self):
        sentinel = self.base / "sentinel"
        self.edit(self.skill(), "## Purpose\n", f"## Purpose\n\n$(touch {sentinel})\n")
        self.assert_error("body contains dynamic pattern")
        self.assertFalse(sentinel.exists())

    def test_metadata_mapping_rules(self):
        self.fm("metadata:\n  suite: fred", "metadata: fred\n  suite: fred")
        self.assert_error("metadata must be an indented mapping")

    def test_metadata_duplicate(self):
        self.fm("  suite: fred\n", "  suite: fred\n  suite: other\n")
        self.assert_error("duplicate key metadata.suite")

    def test_bad_indentation(self):
        self.fm("license: MIT", "   license: MIT")
        self.assert_error("unexpected indentation")


class Budgets(BundleFixture):
    def test_line_budget(self):
        self.edit(self.skill(), "## Handoffs\n", "## Handoffs\n" + "x\n" * 80)
        self.assert_error("lines exceeds budget 150")

    def test_word_budget(self):
        self.edit(self.skill(), "## Handoffs\n", "## Handoffs\n" + ("word " * 500 + "\n"))
        self.assert_error("words exceeds budget 1000")

    def test_combined_description_budget(self):
        self.edit(self.skill(), "description: Implement", "description: " + "extra " * 90 + "Implement")
        self.assert_error("combined descriptions")

    def test_rules_budget(self):
        rules = self.skills / "flipper-core/references/engineering-rules.md"
        rules.write_text(rules.read_text(encoding="utf-8") + "padding " * 400, encoding="utf-8")
        self.assert_error("exceeds budget 1200")


class Links(BundleFixture):
    def test_traversal_escape(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\nSee [x](../../../etc/passwd).\n")
        self.assert_error("link escapes")

    def test_absolute_link(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\nSee [x](/etc/passwd).\n")
        self.assert_error("absolute or backslash link")

    def test_broken_link_and_anchor(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\n[a](nope.md) [b](../flipper-core/references/runtime-ui.md#no-such-heading)\n")
        code, out = self.check()
        self.assertIn("broken link 'nope.md'", out)
        self.assertIn("missing anchor '#no-such-heading'", out)

    def test_valid_anchor(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\n[q](../flipper-core/references/runtime-ui.md#queues-and-cancellation)\n")
        code, out = self.check()
        self.assertEqual(code, 0, out)

    def test_links_in_code_are_ignored(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\n`[x](../../outside.md)`\n\n```\n[y](missing.md)\n```\n")
        code, out = self.check()
        self.assertEqual(code, 0, out)

    @unittest.skipIf(os.name == "nt", "symlink creation needs privileges on Windows")
    def test_symlink_escape(self):
        outside = self.base / "outside.md"
        outside.write_text("# Outside\n", encoding="utf-8")
        (self.skills / "flipper-core/references/leak.md").symlink_to(outside)
        self.assert_error("symlink points outside the bundle")

    def test_payload_must_not_reference_oracles(self):
        self.edit(self.skill(), "## Purpose\n", "## Purpose\n\nCompare with evaluations/oracles/06.json.\n")
        self.assert_error("must not reference evaluation")


class Registry(BundleFixture):
    def setUp(self):
        super().setUp()
        self.path = self.skills / "flipper-core/references/sources.json"
        self.data = json.loads(self.path.read_text(encoding="utf-8"))

    def save(self):
        self.path.write_text(json.dumps(self.data, indent=2), encoding="utf-8")

    def claim(self, cid):
        return next(c for c in self.data["claims"] if c["id"] == cid)

    def test_duplicate_id(self):
        self.data["claims"].append(dict(self.claim("C-MEM-001")))
        self.save()
        self.assert_error("duplicate claim id")

    def test_duplicate_json_key(self):
        text = self.path.read_text(encoding="utf-8").replace('"schema_version": 1', '"schema_version": 1, "schema_version": 1', 1)
        self.path.write_text(text, encoding="utf-8")
        self.assert_error("duplicate key 'schema_version'")

    def test_bad_status(self):
        self.claim("C-MEM-001")["status"] = "verified"
        self.save()
        self.assert_error("status must be one of")

    def test_missing_field(self):
        del self.claim("C-MEM-001")["consequence"]
        self.save()
        self.assert_error("missing 'consequence'")

    def test_moving_branch_url(self):
        self.claim("C-MEM-001")["sources"][0]["url"] = "https://github.com/flipperdevices/flipperzero-firmware/blob/dev/furi/core/memmgr.c"
        self.save()
        self.assert_error("moving branch reference")

    def test_unpinned_url(self):
        src = self.claim("C-MEM-001")["sources"][0]
        src["url"] = "https://github.com/flipperdevices/flipperzero-firmware/blob/1.5.1/furi/core/memmgr.c"
        self.save()
        self.assert_error("not commit-pinned")

    def test_url_must_match_pin(self):
        src = self.claim("C-MEM-001")["sources"][0]
        src["pin"] = "ufbt"
        self.save()
        self.assert_error("does not contain commit of pin 'ufbt'")

    def test_future_date(self):
        self.claim("C-MEM-001")["checked_on"] = "2030-01-01"
        self.save()
        self.assert_error("checked_on is in the future")

    def test_stale_current_claim(self):
        code, out = self.check("--today", "2027-06-01")
        self.assertEqual(code, 1)
        self.assertIn("stale: checked 2026-10-16", out)

    def test_review_required_is_warning_not_stale(self):
        for c in self.data["claims"]:
            c["review_state"] = "review-required"
        self.save()
        code, out = self.check("--today", "2027-06-01")
        self.assertEqual(code, 0, out)
        self.assertIn("review-required", out)

    def test_synthetic_upstream_change_flags_consumers(self):
        # Maintenance drill (docs/maintenance.md): an upstream timer change marks the
        # claim review-required; every consuming page is then reported for review.
        claim = self.claim("C-TMR-001")
        claim["review_state"] = "review-required"
        claim["notes"] = "synthetic drill: furi_timer_stop implementation changed upstream"
        self.save()
        code, out = self.check()
        self.assertEqual(code, 0, out)
        flagged = {line.split(":")[1].strip() for line in out.splitlines()
                   if line.startswith("warning:") and "C-TMR-001" in line}
        self.assertEqual(flagged, set(claim["consumers"]))
        self.assertIn("flipper-core/references/runtime-ui.md", flagged)

    def test_deprecated_claim_cited(self):
        self.claim("C-TMR-001")["review_state"] = "deprecated"
        self.save()
        self.assert_error("whose review_state is 'deprecated'")

    def test_unknown_citation(self):
        self.edit(self.skills / "flipper-core/references/runtime-ui.md", "[C-MEM-001]", "[C-MEM-999]")
        self.assert_error("cites unknown claim C-MEM-999")

    def test_consumer_must_cite(self):
        self.claim("C-JS-001")["consumers"].append("flipper-core/references/storage-parsers.md")
        self.save()
        self.assert_error("does not cite C-JS-001")

    def test_citation_must_be_listed(self):
        self.edit(self.skills / "flipper-core/references/storage-parsers.md", "## Untrusted content", "## Untrusted content\n\nSee [C-JS-001].")
        self.assert_error("cites C-JS-001 but is not listed in its consumers")

    def test_consumer_outside_bundle(self):
        self.claim("C-JS-001")["consumers"].append("../../etc/passwd")
        self.save()
        self.assert_error("is not a file inside the bundle")


class RepositoryRecords(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "repo"
        shutil.copytree(ROOT, self.root, ignore=shutil.ignore_patterns(".git", "__pycache__"))

    def tearDown(self):
        self.tmp.cleanup()

    def check(self):
        return run("--root", self.root, "--today", TODAY)

    def load(self, rel):
        return json.loads((self.root / rel).read_text(encoding="utf-8"))

    def dump(self, rel, data):
        (self.root / rel).write_text(json.dumps(data, indent=2), encoding="utf-8")

    def test_copy_passes(self):
        code, out = self.check()
        self.assertEqual(code, 0, out)

    def test_support_claim_requires_evidence(self):
        data = self.load("compatibility.json")
        data["surfaces"][0]["support"] = "compiled"
        self.dump("compatibility.json", data)
        code, out = self.check()
        self.assertEqual(code, 1)
        self.assertIn("support 'compiled' claimed but evidence.compiled is not 'pass'", out)

    def test_pass_requires_existing_evidence_path(self):
        data = self.load("compatibility.json")
        data["evidence"]["compiled"] = {"status": "pass", "reason": "x", "evidence": "nowhere/log.txt"}
        self.dump("compatibility.json", data)
        code, out = self.check()
        self.assertIn("evidence path not found", out)

    def test_sdk_bytes_verified_needs_matching_digest(self):
        data = self.load("compatibility.json")
        data["baseline"]["sdk_artifact"]["bytes_verified"] = True
        self.dump("compatibility.json", data)
        code, out = self.check()
        self.assertIn("bytes_verified=true requires observed_sha256", out)

    def test_baseline_component_cannot_be_omitted(self):
        data = self.load("compatibility.json")
        del data["baseline"]["toolchain"]
        self.dump("compatibility.json", data)
        code, out = self.check()
        self.assertIn("baseline.toolchain missing", out)

    def test_activation_counts(self):
        data = self.load("evaluations/activation.json")
        data["cases"] = data["cases"][:-1]
        self.dump("evaluations/activation.json", data)
        code, out = self.check()
        self.assertIn("expected 8/8/8 cases", out)

    def test_negative_control_expects_nothing(self):
        data = self.load("evaluations/activation.json")
        neg = next(c for c in data["cases"] if c["kind"] == "negative")
        neg["expected"] = ["flipper-core"]
        self.dump("evaluations/activation.json", data)
        code, out = self.check()
        self.assertIn("negative control must expect no skill", out)

    def test_fixture_must_not_leak_oracle(self):
        p = self.root / "evaluations/fixtures/06-lifecycle-review/uart_logger.c"
        p.write_text(p.read_text(encoding="utf-8") + "\n// T06-D5 join under lock\n", encoding="utf-8")
        code, out = self.check()
        self.assertIn("contains oracle/seeded-defect markers", out)

    def test_oracle_weights_match_rubric(self):
        data = self.load("evaluations/oracles/03-uart.json")
        data["assertions"][0]["weight"] += 1
        self.dump("evaluations/oracles/03-uart.json", data)
        code, out = self.check()
        self.assertIn("do not match rubric", out)

    def test_repository_link_checked(self):
        readme = self.root / "README.md"
        readme.write_text(readme.read_text(encoding="utf-8") + "\n[gone](docs/missing.md)\n", encoding="utf-8")
        code, out = self.check()
        self.assertIn("broken link 'docs/missing.md'", out)


class Usage(unittest.TestCase):
    def test_bad_today(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as cm:
            check_bundle.main(["--today", "yesterday"])
        self.assertEqual(cm.exception.code, 2)

    def test_missing_root(self):
        with contextlib.redirect_stderr(io.StringIO()):
            code = check_bundle.main(["--root", "/nonexistent-fred-root"])
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
