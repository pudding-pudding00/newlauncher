"""Smoke-Test: startet die DOL in Dolphin und wartet auf Marker im Log (Standard: NL_READY).

Exit-Codes: 0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt.
"""

import argparse
import subprocess
import sys
from pathlib import Path

from deploy_dolphin import DEFAULT_DOL, DIST_DIR, FIXTURES_DIR, deploy
from dolphin import (DEFAULT_DOLPHIN, DEFAULT_USER_DIR, dolphin_command, log_path, reset_log,
                     sd_sync_dir, wait_for_markers, write_dev_config)

DEFAULT_MARKERS = ["NL_READY"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dolphin", type=Path, default=DEFAULT_DOLPHIN)
    parser.add_argument("--user-dir", type=Path, default=DEFAULT_USER_DIR)
    parser.add_argument("--dol", type=Path, default=DEFAULT_DOL)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--marker", action="append", dest="markers",
                        help="Log-Marker, der erscheinen muss (mehrfach angebbar)")
    parser.add_argument("--no-fixtures", dest="fixtures", action="store_false",
                        help="Fixture-Bibliothek nicht auf die SD kopieren")
    parser.add_argument("--autostart", metavar="KEY",
                        help="Spielschlüssel, den der Launcher nach NL_READY startet ([debug] autostart)")
    args = parser.parse_args(argv)
    markers = args.markers or DEFAULT_MARKERS

    for name, path in (("Dolphin", args.dolphin), ("DOL", args.dol)):
        if not path.is_file():
            print(f"SMOKE FEHLER: {name} nicht gefunden: {path}")
            return 2

    user_dir = args.user_dir.resolve()
    try:
        write_dev_config(user_dir)
        # Nur der Launcher selbst wird als boot.dol installiert. Andere DOLs (z. B. nl_selftest.dol)
        # startet Dolphin direkt per -e; sie dürfen den Launcher auf der SD nicht ersetzen.
        is_launcher = args.dol.name == DEFAULT_DOL.name
        sd = sd_sync_dir(user_dir)
        deploy(args.dol if is_launcher else None, sd, DIST_DIR, FIXTURES_DIR if args.fixtures else None,
               wii_root=user_dir / "Wii")
        if args.fixtures:
            # Ohne alten Index startet der Launcher mit einem vollständigen Scan der Fixtures.
            (sd / "newlauncher/cache/index.bin").unlink(missing_ok=True)
        if args.autostart:
            with (sd / "newlauncher/config.ini").open("a", encoding="utf-8", newline="\n") as f:
                f.write(f"[debug]\nautostart = {args.autostart}\n")
        reset_log(user_dir)
    except OSError as e:
        print(f"SMOKE FEHLER: Dev-Ordner nicht vorbereitet (läuft Dolphin noch?): {e}")
        return 2

    try:
        proc = subprocess.Popen(dolphin_command(args.dolphin, user_dir, args.dol.resolve()))
    except OSError as e:
        print(f"SMOKE FEHLER: Dolphin startet nicht: {e}")
        return 2
    try:
        missing = wait_for_markers(log_path(user_dir), markers, args.timeout,
                                   is_alive=lambda: proc.poll() is None)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    if not missing:
        print("SMOKE OK")
        return 0
    print(f"SMOKE FEHLGESCHLAGEN: fehlende Marker nach {args.timeout:g} s: {', '.join(missing)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
