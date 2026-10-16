"""Tests for scripts/fred_setup.py (offline: no network, no SDK, no device)."""

import builtins
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import fred_setup  # noqa: E402

SKILLS = fred_setup.SKILLS


def run(*argv, stdin_lines=None):
    out, err = io.StringIO(), io.StringIO()
    patches = [contextlib.redirect_stdout(out), contextlib.redirect_stderr(err)]
    with contextlib.ExitStack() as stack:
        for p in patches:
            stack.enter_context(p)
        if stdin_lines is not None:
            answers = iter(stdin_lines)
            stack.enter_context(mock.patch.object(builtins, "input", lambda *_: next(answers)))
            stack.enter_context(mock.patch.object(sys.stdin, "isatty", lambda: True))
        else:
            stack.enter_context(mock.patch.object(sys.stdin, "isatty", lambda: False))
        code = fred_setup.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.project = self.base / "project"
        self.project.mkdir()
        self.skills = self.project / ".claude" / "skills"
        self.env = mock.patch.dict(os.environ, {"CLAUDE_CONFIG_DIR": str(self.base / "personal")})
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()


class Install(Base):
    def test_project_install_and_verify(self):
        code, out, _ = run("install", "--scope", "project", "--project", self.project, "--yes")
        self.assertEqual(code, 0, out)
        for name in SKILLS:
            self.assertTrue((self.skills / name / "SKILL.md").is_file())
        record = json.loads((self.skills / ".fred-install.json").read_text())
        self.assertEqual(record["version"], "0.1.0-preview")
        self.assertIn("shared references resolve", out)

    def test_personal_scope_uses_claude_config_dir(self):
        code, out, _ = run("install", "--scope", "personal", "--yes")
        self.assertEqual(code, 0, out)
        self.assertTrue((self.base / "personal" / "skills" / "flipper-core" / "SKILL.md").is_file())

    def test_reinstall_is_idempotent(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        code, out, _ = run("install", "--scope", "project", "--project", self.project, "--yes")
        self.assertEqual(code, 0)
        self.assertIn("nothing to do", out)
        self.assertFalse((self.project / ".claude" / "fred-backups").exists())

    def test_changed_copy_is_backed_up_then_replaced(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        skill = self.skills / "flipper-review" / "SKILL.md"
        skill.write_text(skill.read_text() + "\nlocal edit\n")
        code, out, _ = run("install", "--scope", "project", "--project", self.project, "--yes")
        self.assertEqual(code, 0, out)
        self.assertIn("differs in 1 file(s): SKILL.md", out)
        self.assertNotIn("local edit", skill.read_text())
        backups = list((self.project / ".claude" / "fred-backups").glob("*/flipper-review/SKILL.md"))
        self.assertEqual(len(backups), 1)
        self.assertIn("local edit", backups[0].read_text())

    def test_needs_confirmation_when_not_interactive(self):
        code, _, err = run("install", "--scope", "project", "--project", self.project)
        self.assertEqual(code, 2)
        self.assertIn("needs confirmation", err)
        self.assertFalse(self.skills.exists())

    def test_dry_run_changes_nothing(self):
        code, out, _ = run("install", "--scope", "project", "--project", self.project, "--dry-run")
        self.assertEqual(code, 0)
        self.assertIn("(dry run)", out)
        self.assertFalse(self.skills.exists())

    def test_interactive_choice_and_decline(self):
        # choose "project", accept default folder, then decline the install
        with mock.patch.object(os, "getcwd", lambda: str(self.project)):
            code, out, _ = run("install", stdin_lines=["1", "", "n"])
        self.assertEqual(code, 0)
        self.assertIn("Nothing changed", out)
        self.assertFalse(self.skills.exists())

    def test_refuses_symlinked_skill_folder(self):
        if os.name == "nt":
            self.skipTest("symlinks need privileges on Windows")
        self.skills.mkdir(parents=True)
        (self.base / "elsewhere").mkdir()
        (self.skills / "flipper-core").symlink_to(self.base / "elsewhere")
        code, _, err = run("install", "--scope", "project", "--project", self.project, "--yes")
        self.assertEqual(code, 2)
        self.assertIn("symlink", err)

    def test_refuses_broken_source_bundle(self):
        src = self.base / "src"
        shutil.copytree(ROOT / "skills", src)
        (src / "flipper-core" / "references" / "evidence-policy.md").unlink()
        code, out, err = run("install", "--source", src, "--scope", "project", "--project", self.project, "--yes")
        self.assertEqual(code, 2)
        self.assertIn("source bundle has errors", err)
        self.assertFalse(self.skills.exists())

    def test_refuses_repository_skills_folder_as_target(self):
        code, _, err = run("install", "--target", ROOT / "skills", "--yes")
        self.assertEqual(code, 2)
        self.assertIn("own skills/ folder", err)


class Snippet(Base):
    def test_snippet_appended_once_and_existing_text_kept(self):
        (self.project / "CLAUDE.md").write_text("# Mine\nkeep me\n")
        run("install", "--scope", "project", "--project", self.project, "--yes")
        run("install", "--scope", "project", "--project", self.project, "--yes")
        text = (self.project / "CLAUDE.md").read_text()
        self.assertTrue(text.startswith("# Mine\nkeep me\n"))
        self.assertEqual(text.count(fred_setup.SNIPPET_MARKER), 1)

    def test_no_snippet_flag(self):
        run("install", "--scope", "project", "--project", self.project, "--yes", "--no-snippet")
        self.assertFalse((self.project / "CLAUDE.md").exists())


class Uninstall(Base):
    def test_uninstall_removes_only_fred_and_keeps_backup(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        other = self.skills / "my-own-skill"
        other.mkdir()
        (other / "SKILL.md").write_text("x")
        code, out, _ = run("uninstall", "--project", self.project, "--yes")
        self.assertEqual(code, 0, out)
        self.assertEqual(sorted(p.name for p in self.skills.iterdir()), ["my-own-skill"])
        self.assertTrue(list((self.project / ".claude" / "fred-backups").glob("*-uninstall/flipper-core")))


class Doctor(Base):
    def test_healthy_install(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        code, out, _ = run("doctor", "--project", self.project, "--sdk-home", self.base / "nosdk")
        self.assertEqual(code, 0, out)
        self.assertIn("project: all four skills", out)

    def test_detects_broken_install(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        (self.skills / "flipper-core" / "references" / "engineering-rules.md").unlink()
        code, out, _ = run("doctor", "--project", self.project, "--sdk-home", self.base / "nosdk")
        self.assertEqual(code, 1)
        self.assertIn("broken installation", out)

    def test_detects_mixed_versions(self):
        run("install", "--scope", "project", "--project", self.project, "--yes")
        md = self.skills / "flipper-verify" / "SKILL.md"
        md.write_text(md.read_text().replace("version: 0.1.0-preview", "version: 0.0.9-preview"))
        code, out, _ = run("doctor", "--project", self.project, "--sdk-home", self.base / "nosdk")
        self.assertEqual(code, 1)
        self.assertIn("mixed versions", out)

    def test_detects_wrong_sdk_table(self):
        table = self.base / "sdk" / "ufbt-home" / "current" / "sdk_headers" / "f7_sdk" / "targets" / "f7"
        table.mkdir(parents=True)
        (table / "api_symbols.csv").write_text("entry,status,name,type,params\nVersion,+,89.0,,\n")
        code, out, _ = run("doctor", "--project", self.project, "--sdk-home", self.base / "sdk")
        self.assertEqual(code, 1)
        self.assertIn("does not match the pinned 1.5.1 table", out)


class Sdk(Base):
    def test_dry_run_plan_and_no_side_effects(self):
        home = self.base / "sdk"
        code, out, _ = run("sdk", "--home", home, "--dry-run")
        self.assertEqual(code, 0)
        self.assertIn("--require-hashes", out)
        self.assertIn(fred_setup.SDK_SHA256, out)
        self.assertIn("--hw-target=f7", out)
        self.assertFalse(home.exists())

    def test_hash_mismatch_aborts(self):
        bad = self.base / "fake.zip"
        bad.write_bytes(b"not the sdk")
        with self.assertRaises(fred_setup.Abort) as cm:
            fred_setup.verify_sha256(bad, fred_setup.SDK_SHA256, fred_setup.UI(yes=True))
        self.assertIn("NOT used", str(cm.exception))

    def test_requirements_pin_every_package_by_hash(self):
        lines = [l for l in fred_setup.UFBT_REQUIREMENTS.splitlines() if "==" in l]
        self.assertEqual({l.split("==")[0] for l in lines}, {"ufbt", "oslex", "mslex"})
        self.assertEqual(fred_setup.UFBT_REQUIREMENTS.count("--hash=sha256:"), 6)

    def test_build_requires_sdk(self):
        app = self.base / "app"
        shutil.copytree(ROOT / "evaluations" / "reference" / "01-utility", app)
        code, _, err = run("build", app, "--sdk-home", self.base / "nosdk", "--yes")
        self.assertEqual(code, 2)
        self.assertIn("not set up yet", err)


class Card(Base):
    def test_card_written_and_not_overwritten_without_confirmation(self):
        code, out, _ = run("card", "--project", self.project, "--yes")
        self.assertEqual(code, 0, out)
        card = self.project / "docs" / "compatibility-card.md"
        self.assertIn("| Hardware target / board revision | f7 |", card.read_text())
        card.write_text("custom")
        code, _, err = run("card", "--project", self.project)
        self.assertEqual(code, 2)
        self.assertEqual(card.read_text(), "custom")


if __name__ == "__main__":
    unittest.main()
