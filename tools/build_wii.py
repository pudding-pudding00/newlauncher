"""Baut newlauncher.dol (Preset `wii`). Aufruf: python tools/build_wii.py"""

import os
import subprocess
import sys
from pathlib import Path

from devkitpro import wii_build_invocation

REPO_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    cmd, extra_env = wii_build_invocation(REPO_ROOT, sys.platform)
    env = {**os.environ, **extra_env}
    try:
        return subprocess.run(cmd, cwd=REPO_ROOT, env=env).returncode
    except FileNotFoundError:
        print(f"BUILD FEHLER: {cmd[0]} nicht gefunden – ist devkitPro installiert?")
        return 2


if __name__ == "__main__":
    sys.exit(main())
