#!/usr/bin/env python3
"""Run a libwiitls hardware test suite on a real Wii.

Starts a TCP log listener and tests/host/tls_test_server.py (bound to 0.0.0.0),
sends the DOL with wiiload and prints the log. Exit code 0 only if the runner
reports "WTLS-HWTEST DONE pass=P fail=0".

  python tools/hwtest.py --wii 192.168.1.50 --dol build-wii/tests/hw/wtls_hwtest.dol --suite selftest
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DONE_RE = re.compile(r"WTLS-HWTEST DONE pass=(\d+) fail=(\d+)")


def local_ip_towards(ip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((ip, 9))
        return s.getsockname()[0]
    finally:
        s.close()


def find_wiiload():
    candidates = []
    for base in (os.environ.get("DEVKITPRO"), "C:/devkitPro", "/opt/devkitpro"):
        if base:
            candidates += [os.path.join(base, "tools", "bin", n) for n in ("wiiload.exe", "wiiload")]
    for c in candidates:
        if os.path.isfile(c):
            return c
    found = shutil.which("wiiload")
    if found:
        return found
    sys.exit("wiiload not found (install devkitPro tools)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wii", required=True, help="IP address of the Wii (Homebrew Channel running)")
    ap.add_argument("--dol", required=True)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--logport", type=int, default=4405)
    ap.add_argument("--extra", nargs="*", default=[], help="extra key=value runner args")
    args = ap.parse_args()

    my_ip = local_ip_towards(args.wii)
    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("0.0.0.0", args.logport))
    ls.listen(1)
    ls.settimeout(args.timeout)

    # Test PKI whose leaf certificates also carry this PC's IP as iPAddress SAN.
    cert_dir = tempfile.mkdtemp(prefix="wtls-hwcerts-")
    subprocess.run([sys.executable, os.path.join(ROOT, "tests", "host", "make_certs.py"),
                    "--out", cert_dir, "--extra-ip", my_ip], check=True)
    server = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tests", "host", "tls_test_server.py"), "--bind", "0.0.0.0",
         "--certs", cert_dir],
        stdout=subprocess.PIPE, text=True)
    try:
        if server.stdout.readline().strip() != "READY":
            sys.exit("test server failed to start")
        env = dict(os.environ, WIILOAD=f"tcp:{args.wii}")
        runner_args = [f"logip={my_ip}", f"logport={args.logport}", f"suite={args.suite}",
                       f"server={my_ip}", f"pctime={int(time.time())}"] + args.extra
        subprocess.run([find_wiiload(), args.dol] + runner_args, env=env, check=True)

        conn, _ = ls.accept()
        conn.settimeout(args.timeout)
        deadline = time.time() + args.timeout
        log = b""
        result = None
        while time.time() < deadline:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                break
            if not data:
                break
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
            log += data
            result = DONE_RE.search(log.decode("utf-8", "replace"))
            if result:
                break
        conn.close()
        if not result:
            print("\nhwtest: no DONE line (timeout or crash)", file=sys.stderr)
            return 2
        return 0 if int(result.group(2)) == 0 else 1
    finally:
        server.terminate()
        shutil.rmtree(cert_dir, ignore_errors=True)
        ls.close()


if __name__ == "__main__":
    sys.exit(main())
