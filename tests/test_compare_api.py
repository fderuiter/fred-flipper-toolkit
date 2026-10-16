"""Tests for scripts/compare_api.py (standard-library unittest, offline)."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import compare_api  # noqa: E402

DATA = ROOT / "tests" / "data"
HEADER = "entry,status,name,type,params\n"


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = compare_api.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class TempTables(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def table(self, name, body, header=HEADER, raw=None):
        p = self.dir / name
        if raw is not None:
            p.write_bytes(raw)
        else:
            p.write_text(header + body, encoding="utf-8")
        return p


class CompareKnownChange(unittest.TestCase):
    def test_reports_exact_changes_json(self):
        code, out, _ = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--format", "json")
        self.assertEqual(code, 1)
        res = json.loads(out)
        self.assertEqual(res["version"]["old"], "10.4")
        self.assertEqual(res["version"]["new"], "11.0")
        self.assertTrue(res["version"]["major_changed"])
        removed = {(r["entry"], r["name"], r["reason"]) for r in res["removed"]}
        self.assertEqual(removed, {
            ("Header", "lib/demo/legacy.h", "row absent"),
            ("Function", "demo_legacy", "row absent"),
            ("Function", "demo_hidden_soon", "no longer exported"),
        })
        added = {(a["name"], a["reason"]) for a in res["added"]}
        self.assertEqual(added, {("demo_free", "new row"), ("demo_internal", "newly exported")})
        changed = {c["name"]: c for c in res["changed"]}
        self.assertEqual(set(changed), {"demo_write", "demo_version"})
        self.assertEqual(changed["demo_write"]["new"]["params"], "Demo*, const uint8_t*, size_t, uint32_t")
        self.assertEqual(changed["demo_version"]["old"]["type"], "const char*")
        self.assertEqual(sorted(res["regressions"]), ["changed", "major", "removed"])
        self.assertIn("macros", res["limits"])

    def test_whitespace_only_difference_is_not_a_change(self):
        _, out, _ = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--format", "json")
        names = {c["name"] for c in json.loads(out)["changed"]}
        self.assertNotIn("demo_format", names)

    def test_quoted_commas_are_one_field(self):
        table = compare_api.load_table(DATA / "api_old.csv")
        rec = table["symbols"][("Function", "demo_format")]
        self.assertEqual(rec["params"], "char*, size_t, const char*, ...")
        self.assertEqual(rec["type"], "int")

    def test_deterministic_output(self):
        a = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv")
        b = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv")
        self.assertEqual(a, b)
        j1 = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--format", "json")[1]
        j2 = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--format", "json")[1]
        self.assertEqual(j1, j2)

    def test_text_output_lists_changes(self):
        code, out, _ = run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv")
        self.assertEqual(code, 1)
        self.assertIn("10.4 -> 11.0", out)
        self.assertIn("MAJOR CHANGED", out)
        self.assertIn("- Function demo_hidden_soon (no longer exported)", out)
        self.assertIn("~ Function demo_write", out)
        self.assertIn("REGRESSIONS", out)

    def test_identical_tables_exit_zero(self):
        code, out, _ = run("--old", DATA / "api_old.csv", "--new", DATA / "api_old.csv")
        self.assertEqual(code, 0)
        self.assertIn("no selected regressions", out)

    def test_fail_on_selection(self):
        self.assertEqual(run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--fail-on", "none")[0], 0)
        self.assertEqual(run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--fail-on", "added")[0], 1)
        self.assertEqual(run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--fail-on", "bogus")[0], 2)
        self.assertEqual(run("--old", DATA / "api_old.csv", "--new", DATA / "api_new.csv", "--fail-on", ",")[0], 2)

    def test_benchmark_fixture_tables(self):
        fx = ROOT / "evaluations" / "fixtures" / "07-sdk-migration"
        code, out, _ = run("--old", fx / "api_old.csv", "--new", fx / "api_new.csv", "--format", "json")
        self.assertEqual(code, 1)
        res = json.loads(out)
        self.assertEqual([r["name"] for r in res["removed"]], ["meter_legacy_format"])
        self.assertEqual([c["name"] for c in res["changed"]], ["furi_hal_adc_configure"])
        self.assertTrue(res["version"]["major_changed"])


class MalformedInput(TempTables):
    def assert_malformed(self, body, fragment, header=HEADER, raw=None):
        bad = self.table("bad.csv", body, header=header, raw=raw)
        code, _, err = run("--old", DATA / "api_old.csv", "--new", bad)
        self.assertEqual(code, 2, err)
        self.assertIn(fragment, err)

    def test_wrong_header(self):
        self.assert_malformed("Version,+,1.0,,\n", "header must be", header="a,b,c,d,e\n")

    def test_wrong_field_count(self):
        self.assert_malformed("Version,+,1.0,,\nFunction,+,f,void\n", "expected 5 fields")

    def test_bad_status(self):
        self.assert_malformed("Version,+,1.0,,\nFunction,?,f,void,\n", "status must be")

    def test_unknown_entry(self):
        self.assert_malformed("Version,+,1.0,,\nMacro,+,M,,\n", "unknown entry kind")

    def test_missing_version(self):
        self.assert_malformed("Function,+,f,void,\n", "no Version row")

    def test_duplicate_version(self):
        self.assert_malformed("Version,+,1.0,,\nVersion,+,1.1,,\n", "duplicate Version")

    def test_malformed_version(self):
        self.assert_malformed("Version,+,one.two,,\n", "malformed version")

    def test_empty_name_and_type(self):
        self.assert_malformed("Version,+,1.0,,\nFunction,+,,void,\n", "empty name")
        self.assert_malformed("Version,+,1.0,,\nFunction,+,f,,\n", "empty type")

    def test_unterminated_quote(self):
        self.assert_malformed('Version,+,1.0,,\nFunction,+,f,void,"a, b\n', "")

    def test_conflicting_duplicate(self):
        self.assert_malformed("Version,+,1.0,,\nFunction,+,f,void,\nFunction,+,f,int,\n", "conflicting duplicate")

    def test_not_utf8(self):
        self.assert_malformed("", "not valid UTF-8", raw=b"entry,status,name,type,params\n\xff\xfe\n")

    def test_missing_file(self):
        code, _, err = run("--old", self.dir / "nope.csv", "--new", DATA / "api_old.csv")
        self.assertEqual(code, 2)
        self.assertIn("cannot read", err)

    def test_oversize_rejected(self):
        p = self.table("big.csv", "Version,+,1.0,,\n")
        old = compare_api.MAX_BYTES
        compare_api.MAX_BYTES = 10
        try:
            code, _, err = run("--old", p, "--new", p)
        finally:
            compare_api.MAX_BYTES = old
        self.assertEqual(code, 2)
        self.assertIn("larger than", err)

    def test_identical_duplicate_is_warning(self):
        p = self.table("dup.csv", "Version,+,1.0,,\nFunction,+,f,void,\nFunction,+,f,void,\n")
        code, out, _ = run("--old", p, "--new", p)
        self.assertEqual(code, 0)
        self.assertIn("duplicate identical row", out)

    def test_bom_and_blank_lines_tolerated(self):
        p = self.table("bom.csv", "", raw=("﻿" + HEADER + "Version,+,1.0,,\n\nFunction,+,f,void,\n").encode())
        code, _, _ = run("--old", p, "--new", p)
        self.assertEqual(code, 0)

    def test_minor_change_only_is_not_regression_by_default(self):
        a = self.table("a.csv", "Version,+,1.0,,\nFunction,+,f,void,\n")
        b = self.table("b.csv", "Version,+,1.1,,\nFunction,+,f,void,\nFunction,+,g,void,\n")
        self.assertEqual(run("--old", a, "--new", b)[0], 0)
        self.assertEqual(run("--old", a, "--new", b, "--fail-on", "minor")[0], 1)
        res = json.loads(run("--old", b, "--new", a, "--format", "json")[1])
        self.assertEqual(res["version"]["direction"], "downgrade")


class HelperSafety(unittest.TestCase):
    FORBIDDEN = ("import subprocess", "import socket", "urllib", "http.client", "eval(",
                 "exec(", "os.system", "importlib", "__import__", "pickle", "shutil")

    def test_helpers_have_no_process_network_or_eval(self):
        for name in ("compare_api.py", "check_bundle.py"):
            src = (ROOT / "scripts" / name).read_text(encoding="utf-8")
            for token in self.FORBIDDEN:
                self.assertNotIn(token, src, f"{name} contains {token!r}")

    def test_help_documents_exit_status(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit) as cm:
            compare_api.main(["--help"])
        self.assertEqual(cm.exception.code, 0)
        self.assertIn("Exit 0", out.getvalue())


if __name__ == "__main__":
    unittest.main()
