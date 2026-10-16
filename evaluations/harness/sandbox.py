#!/usr/bin/env python3
"""Linux namespace sandbox for evaluated pilot sessions (issue #14).

An evaluated session runs inside new mount, PID and network namespaces whose
root filesystem is built from an allowlist:

  /usr, /etc (and the /bin, /lib... symlinks)  read-only, from the host
  /opt/fred-claude                             read-only, the Claude Code binary's directory
  /run/fred                                    read-only, settings and CA files written by the harness
  /work                                        read-write, the session's own workspace copy
  /home/fred-eval                              read-write, a fresh HOME for this session
  /tmp, /dev/shm                               fresh tmpfs
  /proc                                        the sandbox's own PID namespace
  /dev                                         null, zero, full, random, urandom only

Nothing else of the host is mounted, so the repository, oracles, task
definitions, research, other sessions' workspaces and the operator's home do
not exist inside. The old root is detached with pivot_root, and the session
runs with no capabilities and no_new_privs (as uid 65534 when the harness
runs as root, otherwise as the operator mapped into a user namespace).

The network namespace has only loopback. A forwarder inside listens on
127.0.0.1:3128 and relays to a Unix socket served by the harness, which
accepts only HTTPS CONNECT to allowlisted hosts (the model API by default) and
logs every request. The repository is public, so this also stops the agent
from fetching oracles over the network.

`probe` runs a fixed set of checks through exactly the same wrapper the pilot
uses and writes a JSON record; it launches no model session. The pilot refuses
to start in sandbox mode unless the probe passes.

Requires Linux with util-linux (unshare, setpriv, pivot_root) and python3
under /usr. As root it needs nothing else. As a normal user it needs
unprivileged user namespaces (Ubuntu 24.04 restricts these through AppArmor;
run the harness with sudo there). It installs nothing.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = "/work"
HOME = "/home/fred-eval"
RUN = "/run/fred"
CLAUDE_DIR = "/opt/fred-claude"
PORT = 3128
NOBODY = 65534
SYSTEM_DIRS = ("usr", "etc")
SYSTEM_LINKS = ("bin", "sbin", "lib", "lib32", "lib64", "libx32")
DEV_NODES = ("null", "zero", "full", "random", "urandom")
CA_ENV = ("NODE_EXTRA_CA_CERTS", "SSL_CERT_FILE", "CURL_CA_BUNDLE", "REQUESTS_CA_BUNDLE")
PASS_ENV = ("ANTHROPIC_API_KEY", "ANTHROPIC_BASE_URL", "LANG")
DEFAULT_ALLOW = "api.anthropic.com"

# In-sandbox: bring up loopback (needs CAP_NET_ADMIN, so before dropping caps).
LO_UP = ("import socket,fcntl,struct;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);"
         "fcntl.ioctl(s,0x8914,struct.pack('16sH14s',b'lo',0x1|0x8|0x40,bytes(14)))")

# In-sandbox: TCP 127.0.0.1:PORT -> the harness's Unix socket. Forks once it listens.
FORWARDER = f"""
import os, socket, threading
L = socket.socket(); L.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
L.bind(('127.0.0.1', {PORT})); L.listen(64)
if os.fork():
    os._exit(0)
os.setsid()
def pipe(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except OSError:
        pass
    try:
        b.shutdown(socket.SHUT_WR)
    except OSError:
        pass
while True:
    c, _ = L.accept()
    u = socket.socket(socket.AF_UNIX)
    try:
        u.connect('{RUN}/proxy.sock')
    except OSError:
        c.close(); continue
    threading.Thread(target=pipe, args=(c, u), daemon=True).start()
    threading.Thread(target=pipe, args=(u, c), daemon=True).start()
"""


def overlaps(a: Path, b: Path) -> bool:
    a, b = a.resolve(), b.resolve()
    return a == b or a in b.parents or b in a.parents


def find_claude(name: str = "claude") -> Path | None:
    w = shutil.which(name)
    return Path(w).resolve() if w else None


class AllowlistProxy:
    """HTTPS CONNECT proxy on a Unix socket that only tunnels to allowlisted hosts on port 443."""

    def __init__(self, path: Path, allow: tuple[str, ...], upstream: str | None = None):
        self.path, self.allow = path, tuple(h.lower() for h in allow)
        self.upstream = urllib.parse.urlsplit(upstream) if upstream else None
        self.log: list[dict] = []
        self._lock = threading.Lock()
        self._srv: socket.socket | None = None

    def decide(self, head: bytes) -> tuple[str, int] | None:
        try:
            method, target, _ = head.split(b"\r\n", 1)[0].decode("latin-1").split(" ", 2)
            host, port = target.rsplit(":", 1)
            port_n = int(port)
        except ValueError:
            return None
        if method.upper() != "CONNECT" or host.lower() not in self.allow or port_n != 443:
            return None
        return host, port_n

    def start(self) -> None:
        if self.path.exists():
            self.path.unlink()
        self._srv = socket.socket(socket.AF_UNIX)
        self._srv.bind(str(self.path))
        os.chmod(self.path, 0o666)
        self._srv.listen(64)
        threading.Thread(target=self._serve, daemon=True).start()

    def close(self) -> None:
        if self._srv:
            self._srv.close()
            self._srv = None

    def _serve(self) -> None:
        while self._srv:
            try:
                c, _ = self._srv.accept()
            except OSError:
                return
            threading.Thread(target=self._handle, args=(c,), daemon=True).start()

    def _record(self, head: bytes, allowed: bool, error: str = "") -> None:
        line = head.split(b"\r\n", 1)[0].decode("latin-1", "replace")[:200]
        with self._lock:
            self.log.append({"request": line, "allowed": allowed, **({"error": error} if error else {})})

    def _open_upstream(self, host: str, port: int) -> socket.socket:
        if not self.upstream:
            return socket.create_connection((host, port), timeout=30)
        u = self.upstream
        s = socket.create_connection((u.hostname, u.port or 80), timeout=30)
        req = f"CONNECT {host}:{port} HTTP/1.1\r\nHost: {host}:{port}\r\n"
        if u.username:
            cred = f"{urllib.parse.unquote(u.username)}:{urllib.parse.unquote(u.password or '')}"
            req += f"Proxy-Authorization: Basic {base64.b64encode(cred.encode()).decode()}\r\n"
        s.sendall((req + "\r\n").encode())
        resp = _read_head(s)
        if b" 200" not in resp.split(b"\r\n", 1)[0]:
            s.close()
            raise OSError(resp.split(b"\r\n", 1)[0].decode("latin-1", "replace"))
        return s

    def _handle(self, c: socket.socket) -> None:
        head = _read_head(c)
        target = self.decide(head)
        if not target:
            self._record(head, False)
            c.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")
            c.close()
            return
        try:
            u = self._open_upstream(*target)
        except OSError as exc:
            self._record(head, True, str(exc)[:200])
            c.sendall(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n")
            c.close()
            return
        self._record(head, True)
        u.settimeout(None)
        c.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
        for a, b in ((c, u), (u, c)):
            threading.Thread(target=_pipe, args=(a, b), daemon=True).start()


def _read_head(s: socket.socket, limit: int = 8192) -> bytes:
    buf = b""
    while b"\r\n\r\n" not in buf and len(buf) < limit:
        d = s.recv(1)
        if not d:
            break
        buf += d
    return buf


def _pipe(a: socket.socket, b: socket.socket) -> None:
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except OSError:
        pass
    try:
        b.shutdown(socket.SHUT_WR)
    except OSError:
        pass


class Sandbox:
    """One sandboxed session. `base` is a host folder that the sandbox never sees as a whole."""

    def __init__(self, base: Path, workspace: Path, *, claude: Path | None = None,
                 allow_hosts: tuple[str, ...] | None = None, credentials: Path | None = None,
                 env: dict | None = None, private: tuple[Path, ...] = (ROOT,)):
        self.base, self.workspace = Path(base), Path(workspace)
        self.env = dict(os.environ if env is None else env)
        self.claude = claude
        if allow_hosts is None:
            api = urllib.parse.urlsplit(self.env.get("ANTHROPIC_BASE_URL", "")).hostname
            allow_hosts = (api or DEFAULT_ALLOW,)
        self.allow_hosts = tuple(allow_hosts)
        self.credentials = credentials
        self.private = tuple(Path(p) for p in private)
        self.as_root = os.geteuid() == 0
        self.newroot, self.home, self.run = self.base / "root", self.base / "home", self.base / "run"
        self.proxy: AllowlistProxy | None = None

    # -- mounts -------------------------------------------------------------
    def ro_binds(self) -> list[tuple[Path, str]]:
        binds = [(Path("/") / d, "/" + d) for d in SYSTEM_DIRS
                 if (Path("/") / d).is_dir() and not (Path("/") / d).is_symlink()]
        if self.claude:
            binds.append((self.claude.parent, CLAUDE_DIR))
        binds.append((self.run, RUN))
        return binds

    def rw_binds(self) -> list[tuple[Path, str]]:
        return [(self.workspace, WORK), (self.home, HOME)]

    def validate(self) -> None:
        """Refuse any mount whose source overlaps a private path (the repository by default)."""
        for src, _ in self.ro_binds() + self.rw_binds():
            for p in self.private:
                if overlaps(src, p):
                    raise SystemExit(f"sandbox: refusing to mount {src}: overlaps private path {p}")

    def inner_env(self) -> dict:
        env = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
               "HOME": HOME, "USER": "fred-eval", "SHELL": "/bin/bash", "TMPDIR": "/tmp",
               "LANG": "C.UTF-8", "HTTPS_PROXY": f"http://127.0.0.1:{PORT}",
               "https_proxy": f"http://127.0.0.1:{PORT}", "HTTP_PROXY": f"http://127.0.0.1:{PORT}",
               "http_proxy": f"http://127.0.0.1:{PORT}", "NO_PROXY": "localhost,127.0.0.1",
               "no_proxy": "localhost,127.0.0.1", "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
               "DISABLE_AUTOUPDATER": "1"}
        for k in PASS_ENV:
            if self.env.get(k):
                env[k] = self.env[k]
        for i, k in enumerate(CA_ENV):
            if self.env.get(k) and os.access(self.env[k], os.R_OK) and os.path.isfile(self.env[k]):
                env[k] = f"{RUN}/ca-{i}.pem"
        return env

    def script(self) -> str:
        q = shlex.quote
        lines = ["set -eu", f"R={q(str(self.newroot))}", 'mount -t tmpfs -o mode=0755 fred-sandbox "$R"']
        for src, dst in self.ro_binds():
            lines += [f'mkdir -p "$R"{q(dst)}', f'mount --rbind {q(str(src))} "$R"{q(dst)}',
                      f'mount -o remount,bind,ro "$R"{q(dst)}']
        for src, dst in self.rw_binds():
            lines += [f'mkdir -p "$R"{q(dst)}', f'mount --bind {q(str(src))} "$R"{q(dst)}']
        for name in SYSTEM_LINKS:
            p = Path("/") / name
            if p.is_symlink():
                lines.append(f'ln -s {q(os.readlink(p))} "$R"/{name}')
        lines += ['mkdir -p "$R/dev/shm" "$R/proc" "$R/tmp"',
                  'mount -t tmpfs -o mode=1777 fred-tmp "$R/tmp"',
                  'mount -t tmpfs -o mode=1777 fred-shm "$R/dev/shm"']
        for n in DEV_NODES:
            lines += [f'touch "$R/dev/{n}"', f'mount --bind /dev/{n} "$R/dev/{n}"']
        lines += ['ln -s /proc/self/fd "$R/dev/fd"', 'ln -s /proc/self/fd/0 "$R/dev/stdin"',
                  'ln -s /proc/self/fd/1 "$R/dev/stdout"', 'ln -s /proc/self/fd/2 "$R/dev/stderr"',
                  'mount -t proc proc "$R/proc"',
                  f"python3 -c {q(LO_UP)}",
                  'cd "$R"', "mkdir .old", "pivot_root . .old", "umount -l /.old", "rmdir /.old",
                  f"cd {WORK}"]
        drop = ["setpriv"]
        if self.as_root:
            drop += ["--reuid", str(NOBODY), "--regid", str(NOBODY), "--clear-groups"]
        drop += ["--inh-caps=-all", "--bounding-set=-all", "--no-new-privs", "--"]
        env = ["env", "-i", *[f"{k}={v}" for k, v in sorted(self.inner_env().items())]]
        lines.append(" ".join(map(q, drop + env + ["python3", "-c", FORWARDER])))
        lines.append('exec ' + " ".join(map(q, drop + env)) + ' "$@"')
        return "\n".join(lines) + "\n"

    # -- lifecycle ----------------------------------------------------------
    def put_run_file(self, name: str, text: str) -> str:
        (self.run / name).write_text(text)
        os.chmod(self.run / name, 0o644)
        return f"{RUN}/{name}"

    def __enter__(self) -> "Sandbox":
        self.validate()
        for d in (self.newroot, self.home, self.run):
            d.mkdir(parents=True, exist_ok=True)
        os.chmod(self.run, 0o755)
        for i, k in enumerate(CA_ENV):
            if self.env.get(k) and os.access(self.env[k], os.R_OK) and os.path.isfile(self.env[k]):
                shutil.copyfile(self.env[k], self.run / f"ca-{i}.pem")
                os.chmod(self.run / f"ca-{i}.pem", 0o644)
        if self.credentials:
            (self.home / ".claude").mkdir(exist_ok=True)
            shutil.copyfile(self.credentials, self.home / ".claude" / ".credentials.json")
            os.chmod(self.home / ".claude" / ".credentials.json", 0o600)
        if self.as_root:
            for d in (self.workspace, self.home):
                for p in [d, *d.rglob("*")]:
                    os.lchown(p, NOBODY, NOBODY)
        (self.base / "sandbox.sh").write_text(self.script())
        upstream = self.env.get("HTTPS_PROXY") or self.env.get("https_proxy")
        self.proxy = AllowlistProxy(self.run / "proxy.sock", self.allow_hosts, upstream)
        self.proxy.start()
        return self

    def __exit__(self, *exc) -> None:
        if self.proxy:
            self.proxy.close()

    def command(self, argv: list[str]) -> list[str]:
        """Host argv that runs `argv` inside the sandbox (cwd /work). A Claude binary is at CLAUDE_DIR."""
        ns = ["unshare", "--mount", "--pid", "--net", "--fork", "--propagation", "private"]
        if not self.as_root:
            ns[1:1] = ["--user", "--map-root-user"]
        return ns + ["sh", str(self.base / "sandbox.sh"), *argv]

    def claude_path(self) -> str:
        return f"{CLAUDE_DIR}/{self.claude.name}" if self.claude else "claude"

    def record(self) -> dict:
        return {"backend": "unshare", "as_root": self.as_root, "work": WORK, "home": HOME,
                "ro_binds": [[str(s), d] for s, d in self.ro_binds()],
                "rw_binds": [[str(s), d] for s, d in self.rw_binds()],
                "allow_hosts": list(self.allow_hosts), "private": [str(p) for p in self.private],
                "credentials_copied": bool(self.credentials)}


# -- probe ----------------------------------------------------------------
TEST_C = r"""#include <stdio.h>
#include <string.h>
static int checksum(const unsigned char *b, size_t n) { int s = 0; for (size_t i = 0; i < n; i++) s ^= b[i]; return s; }
int main(void) { const unsigned char f[] = {0xA5, 0x01, 0x02, 0xA6};
  if (checksum(f, 3) != f[3]) { puts("FAIL"); return 1; } puts("PASS"); return 0; }
"""


HOST_UNSAFE = ("write_usr_denied", "write_etc_denied", "write_run_denied", "remount_denied")


def probe_checks(root: Path, deny_host: str, allow_host: str | None, cc: list[str],
                 claude_cmd: str | None = None) -> list[tuple[str, str, bool]]:
    """(name, shell command, expected to succeed). Every command runs inside the sandbox."""
    r = shlex.quote(str(root))
    checks = [
        ("no_effective_capabilities", "! grep -q '^CapEff:.*[1-9a-f]' /proc/self/status", True),
        ("repository_absent", f"test ! -e {r}", True),
        ("oracle_unreadable", f"cat {r}/evaluations/oracles/01-utility.json", False),
        ("rubric_unreadable", f"cat {r}/evaluations/rubric.json", False),
        ("no_evaluation_files_anywhere",
         "find / -path /proc -prune -o \\( -name rubric.json -o -name heldout.json -o -name oracles "
         "-o -name 'research-design*' -o -name run_pilot.py \\) -print 2>/dev/null | grep -q .", False),
        ("only_session_home", "test \"$(ls -A /home)\" = fred-eval && test ! -e /root", True),
        ("host_processes_hidden", "test \"$(ls /proc | grep -c '^[0-9]')\" -lt 10", True),
        ("old_root_detached", "test ! -e /.old && ! grep -q ' /.old' /proc/self/mounts", True),
        ("write_usr_denied", "touch /usr/fred-probe", False),
        ("write_etc_denied", "touch /etc/fred-probe", False),
        ("write_run_denied", "touch /run/fred/fred-probe", False),
        ("remount_denied", "mount -o remount,rw /usr", False),
        ("write_workspace", "touch /work/.probe && rm /work/.probe", True),
        ("direct_egress_blocked",
         "python3 -c \"import socket;socket.create_connection(('1.1.1.1',443),3)\"", False),
        ("proxy_denies_repository_host",
         f"curl -sS --max-time 20 -o /dev/null https://{deny_host}/repos/fderuiter/fred-flipper-toolkit", False),
    ]
    for c in cc:
        san = " -fsanitize=address,undefined" if c == "gcc" else ""
        checks.append((f"compound_compile_and_test_{c}",
                       f"cd /work && {c} -std=c11 -Wall -Wextra -Werror{san} "
                       f"-o probe_{c} probe_test.c && ./probe_{c} | grep -q PASS && rm probe_{c}", True))
    checks.append(("make_runs", "cd /work && printf 'all:\\n\\t@echo made\\n' > probe.mk && "
                                "make -f probe.mk | grep -q made && rm probe.mk", True))
    if claude_cmd:
        checks.append(("claude_binary_starts", f"{shlex.quote(claude_cmd)} --version", True))
    if allow_host:
        checks.append(("proxy_allows_api_host",
                       f"curl -sS --max-time 30 -o /dev/null https://{allow_host}/", True))
    return checks


def run_probe(claude: Path | None, fixture: str = "03-uart", allow: tuple[str, ...] | None = None,
              credentials: Path | None = None, offline: bool = False, control: bool = False) -> dict:
    """Run every probe check inside a fresh sandbox (or, with control=True, directly on the host)."""
    with tempfile.TemporaryDirectory(prefix="fred-sandbox-probe-") as tmp:
        base = Path(tmp)
        os.chmod(base, 0o755)
        ws = base / "ws"
        shutil.copytree(ROOT / "evaluations" / "fixtures" / fixture, ws)
        (ws / "probe_test.c").write_text(TEST_C)
        with Sandbox(base, ws, claude=claude, allow_hosts=allow, credentials=credentials) as sb:
            cc = [c for c in ("gcc", "clang") if shutil.which(c)]
            allow_check = None if offline else sb.allow_hosts[0]
            checks = probe_checks(ROOT, "api.github.com", allow_check, cc,
                                  (str(claude) if control else sb.claude_path()) if claude else None)
            body = "\n".join(f"( {cmd} ) >/dev/null 2>&1; echo \"FREDPROBE {name} $?\"" for name, cmd, _ in checks)
            if control:
                # Never write to or remount host system folders.
                checks = [c for c in checks if c[0] not in HOST_UNSAFE]
                body = "\n".join(f"( {cmd} ) >/dev/null 2>&1; echo \"FREDPROBE {name} $?\""
                                 for name, cmd, _ in checks).replace("/work", str(ws))
                argv = ["sh", "-c", body]
            else:
                argv = sb.command(["sh", "-c", body])
            p = subprocess.run(argv, capture_output=True, text=True, timeout=600)
            seen = {}
            for line in p.stdout.splitlines():
                parts = line.split()
                if len(parts) == 3 and parts[0] == "FREDPROBE":
                    seen[parts[1]] = int(parts[2])
            results = []
            for name, cmd, expect in checks:
                rc = seen.get(name)
                ok = rc is not None and ((rc == 0) == expect)
                results.append({"check": name, "command": cmd, "expect_success": expect, "rc": rc,
                                "pass": ok})
            rec = {"mode": "control (no sandbox)" if control else "sandbox",
                   "sandbox": sb.record(), "claude": str(claude) if claude else None,
                   "claude_version": None, "euid": os.geteuid(), "wrapper_rc": p.returncode,
                   "wrapper_stderr": p.stderr[-2000:], "kernel": os.uname().release,
                   "fixture": fixture, "checks": results,
                   "proxy_log": sb.proxy.log if sb.proxy else [],
                   "pass": p.returncode == 0 and all(r["pass"] for r in results)}
            if claude and not control:
                v = subprocess.run(sb.command([sb.claude_path(), "--version"]), capture_output=True,
                                   text=True, timeout=120)
                rec["claude_version"] = v.stdout.strip() or None
    return rec


def probe(args) -> int:
    claude = Path(args.claude).resolve() if args.claude else find_claude()
    rec = run_probe(claude, args.fixture, tuple(args.allow_host) if args.allow_host else None,
                    Path(args.credentials) if args.credentials else None, args.offline, args.control)
    results = rec["checks"]
    text = json.dumps(rec, indent=2) + "\n"
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text)
    for r in results:
        print(f"{'PASS' if r['pass'] else 'FAIL'} {r['check']} (rc={r['rc']}, expected "
              f"{'success' if r['expect_success'] else 'failure'})")
    if rec["wrapper_rc"] != 0 and all(r["rc"] is None for r in results):
        print(rec["wrapper_stderr"], file=sys.stderr)
    if args.control:
        # The control is expected to fail: it shows the checks detect a readable repository.
        print("control run (no sandbox):", "checks failed as expected" if not rec["pass"] else
              "UNEXPECTED: all checks passed without a sandbox")
        return 0 if not rec["pass"] else 1
    print("sandbox probe:", "PASS" if rec["pass"] else "FAIL")
    return 0 if rec["pass"] else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("probe", help="prove isolation and host-test ability without any model session")
    p.add_argument("--out", help="write the JSON record here")
    p.add_argument("--fixture", default="03-uart")
    p.add_argument("--claude", help="Claude Code binary (default: resolved from PATH)")
    p.add_argument("--allow-host", action="append", help="host the proxy may tunnel to (repeatable)")
    p.add_argument("--credentials", help="copy this .credentials.json into the sandbox HOME")
    p.add_argument("--offline", action="store_true", help="skip the allowlisted-host reachability check")
    p.add_argument("--control", action="store_true",
                   help="negative control: run the same checks on the host without a sandbox (they must fail)")
    args = ap.parse_args(argv)
    return probe(args)


if __name__ == "__main__":
    sys.exit(main())
