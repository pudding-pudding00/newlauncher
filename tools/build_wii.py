"""Baut newlauncher.dol (Preset `wii`). Aufruf: python tools/build_wii.py [--no-tls]

Mit TLS nach build/wii, ohne TLS nach build/wii-notls (Smokes und Deploy nehmen build/wii)."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from devkitpro import wii_build_invocation
from mbedtls_src import ensure_source

REPO_ROOT = Path(__file__).resolve().parents[1]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Wii-Build von newlauncher")
    ap.add_argument("--no-tls", action="store_true", help="ohne HTTPS bauen (NL_TLS=OFF)")
    args = ap.parse_args(argv)
    if sys.platform == "win32" and not args.no_tls:
        ensure_source()
    cmd, extra_env = wii_build_invocation(REPO_ROOT, sys.platform, tls=not args.no_tls)
    env = {**os.environ, **extra_env}
    try:
        return subprocess.run(cmd, cwd=REPO_ROOT, env=env).returncode
    except FileNotFoundError:
        print(f"BUILD FEHLER: {cmd[0]} nicht gefunden – ist devkitPro installiert?")
        return 2


if __name__ == "__main__":
    sys.exit(main())
