"""Smoke-Test: startet die DOL in Dolphin und wartet auf NL_READY im Log.

Exit-Codes: 0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt.
"""

import argparse
import subprocess
import sys
from pathlib import Path

from deploy_dolphin import DEFAULT_DOL, DIST_DIR, deploy
from dolphin import (DEFAULT_DOLPHIN, DEFAULT_USER_DIR, dolphin_command, log_path, reset_log,
                     sd_sync_dir, wait_for_marker, write_dev_config)

MARKER = "NL_READY"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dolphin", type=Path, default=DEFAULT_DOLPHIN)
    parser.add_argument("--user-dir", type=Path, default=DEFAULT_USER_DIR)
    parser.add_argument("--dol", type=Path, default=DEFAULT_DOL)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args(argv)

    for name, path in (("Dolphin", args.dolphin), ("DOL", args.dol)):
        if not path.is_file():
            print(f"SMOKE FEHLER: {name} nicht gefunden: {path}")
            return 2

    user_dir = args.user_dir.resolve()
    write_dev_config(user_dir)
    deploy(args.dol, sd_sync_dir(user_dir), DIST_DIR)
    reset_log(user_dir)

    proc = subprocess.Popen(dolphin_command(args.dolphin, user_dir, args.dol.resolve()))
    try:
        found = wait_for_marker(log_path(user_dir), MARKER, args.timeout,
                                is_alive=lambda: proc.poll() is None)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    if found:
        print("SMOKE OK")
        return 0
    print(f"SMOKE FEHLGESCHLAGEN: {MARKER} nicht innerhalb von {args.timeout:g} s")
    return 1


if __name__ == "__main__":
    sys.exit(main())
