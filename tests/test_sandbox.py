import os
import socket
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "evaluations" / "harness"))

import run_pilot  # noqa: E402
import sandbox  # noqa: E402


class SandboxSpec(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name) / "s"
        self.ws = self.base / "ws"
        self.ws.mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def sb(self, **kw):
        kw.setdefault("env", {})
        return sandbox.Sandbox(self.base, self.ws, **kw)

    def test_overlaps(self):
        self.assertTrue(sandbox.overlaps(Path("/usr"), Path("/usr/share")))
        self.assertTrue(sandbox.overlaps(Path("/usr/share"), Path("/usr")))
        self.assertFalse(sandbox.overlaps(Path("/usr"), Path("/etc")))

    def test_refuses_mount_overlapping_private_path(self):
        with self.assertRaises(SystemExit):
            self.sb(private=(Path("/usr/share"),)).validate()
        with self.assertRaises(SystemExit):
            sandbox.Sandbox(ROOT / "x", ROOT / "x" / "ws", env={}).validate()
        self.sb().validate()  # temp workspace, repository private: accepted

    def test_mounts_never_include_the_repository(self):
        sb = self.sb(claude=Path("/opt/claude-code/bin/claude"))
        for src, _ in sb.ro_binds() + sb.rw_binds():
            self.assertFalse(sandbox.overlaps(src, ROOT), src)
        self.assertNotIn(str(ROOT), sb.script())

    def test_script_detaches_old_root_and_drops_privileges(self):
        script = self.sb().script()
        self.assertIn("pivot_root . .old", script)
        self.assertIn("umount -l /.old", script)
        for flag in ("--inh-caps=-all", "--bounding-set=-all", "--no-new-privs", "env -i"):
            self.assertIn(flag, script)
        self.assertIn("--net", self.sb().command(["true"]))

    def test_user_namespace_when_not_root(self):
        sb = self.sb()
        sb.as_root = False
        self.assertIn("--map-root-user", sb.command(["true"]))
        self.assertNotIn("--reuid", sb.script())
        sb.as_root = True
        self.assertNotIn("--map-root-user", sb.command(["true"]))
        self.assertIn("--reuid 65534", sb.script())

    def test_inner_env_is_an_allowlist(self):
        ca = Path(self.tmp.name) / "ca.pem"
        ca.write_text("x")
        env = self.sb(env={"SECRET_TOKEN": "s", "ANTHROPIC_API_KEY": "k", "SSL_CERT_FILE": str(ca),
                           "HTTPS_PROXY": "http://10.0.0.1:1"}).inner_env()
        self.assertNotIn("SECRET_TOKEN", env)
        self.assertEqual(env["ANTHROPIC_API_KEY"], "k")
        self.assertTrue(env["SSL_CERT_FILE"].startswith(sandbox.RUN + "/"))
        self.assertEqual(env["HTTPS_PROXY"], f"http://127.0.0.1:{sandbox.PORT}")
        self.assertEqual(env["HOME"], sandbox.HOME)

    def test_default_allowlist_is_the_model_api(self):
        self.assertEqual(self.sb().allow_hosts, ("api.anthropic.com",))
        self.assertEqual(self.sb(env={"ANTHROPIC_BASE_URL": "https://gw.example:8443/v1"}).allow_hosts,
                         ("gw.example",))


class Proxy(unittest.TestCase):
    def test_decide(self):
        p = sandbox.AllowlistProxy(Path("/nonexistent"), ("api.anthropic.com",))
        self.assertEqual(p.decide(b"CONNECT api.anthropic.com:443 HTTP/1.1\r\n\r\n"), ("api.anthropic.com", 443))
        self.assertEqual(p.decide(b"CONNECT API.Anthropic.com:443 HTTP/1.1\r\n\r\n"), ("API.Anthropic.com", 443))
        for head in (b"CONNECT api.github.com:443 HTTP/1.1\r\n\r\n", b"CONNECT api.anthropic.com:80 HTTP/1.1\r\n\r\n",
                     b"GET http://api.anthropic.com/ HTTP/1.1\r\n\r\n", b"garbage", b""):
            self.assertIsNone(p.decide(head), head)

    def test_denied_connect_gets_403_and_is_logged(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = sandbox.AllowlistProxy(Path(tmp) / "p.sock", ("api.anthropic.com",))
            p.start()
            try:
                c = socket.socket(socket.AF_UNIX)
                c.connect(str(Path(tmp) / "p.sock"))
                c.sendall(b"CONNECT github.com:443 HTTP/1.1\r\nHost: github.com:443\r\n\r\n")
                self.assertTrue(c.recv(100).startswith(b"HTTP/1.1 403"))
                c.close()
            finally:
                p.close()
            self.assertEqual(p.log, [{"request": "CONNECT github.com:443 HTTP/1.1", "allowed": False}])


class PilotArms(unittest.TestCase):
    def args(self, sandboxed):
        return SimpleNamespace(sandbox=sandboxed, model="m", effort="medium", budget=1.0)

    def test_sandbox_mode_gives_general_shell(self):
        a = run_pilot.claude_args(self.args(True), "/run/fred/settings.json")
        self.assertIn("Bash", a[a.index("--allowedTools") + 1:a.index("--settings")])
        b = run_pilot.claude_args(self.args(False), "/x")
        self.assertNotIn("Bash", b[b.index("--allowedTools") + 1:b.index("--settings")])

    def test_arm_does_not_change_session_arguments(self):
        # claude_args takes no arm: both arms get identical tools and permissions by construction.
        self.assertNotIn("arm", run_pilot.claude_args.__code__.co_varnames)


@unittest.skipUnless(os.environ.get("FRED_SANDBOX_TEST") == "1", "set FRED_SANDBOX_TEST=1 to run the live probe")
class LiveProbe(unittest.TestCase):
    def test_probe_passes(self):
        rec = sandbox.run_probe(sandbox.find_claude(), offline=True)
        self.assertTrue(rec["pass"], [c for c in rec["checks"] if not c["pass"]])


if __name__ == "__main__":
    unittest.main()
