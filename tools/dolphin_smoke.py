"""Smoke-Test: startet die DOL in Dolphin und wartet auf Marker im Log (Standard: NL_READY).

Exit-Codes: 0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt.
"""

import argparse
import functools
import http.server
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from deploy_dolphin import DEFAULT_DOL, DIST_DIR, FIXTURES_DIR, deploy
from dolphin import (DEFAULT_DOLPHIN, DEFAULT_USER_DIR, dolphin_command, log_path, reset_log,
                     sd_sync_dir, wait_for_markers, write_dev_config)
from pngfile import solid_png

DEFAULT_MARKERS = ["NL_READY"]

# Bilder des Test-Cover-Servers (Pfad wie bei GameTDB: <Region>/<ID6>.png). RMGP01 fehlt absichtlich.
COVER_SERVER_FILES = {"EN/RMCP01.png": (160, 224, (200, 40, 40, 255)),
                      "US/GM8P01.png": (160, 224, (40, 40, 200, 255))}


def write_cover_server_files(root: Path) -> None:
    for rel, (w, h, color) in COVER_SERVER_FILES.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(solid_png(w, h, color))


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):  # noqa: A002 - Signatur der Basisklasse
        pass


def start_cover_server(root: Path) -> tuple[http.server.ThreadingHTTPServer, int]:
    """Startet einen HTTP-Server für root auf 127.0.0.1 und einem freien Port, in einem Thread."""
    handler = functools.partial(_QuietHandler, directory=str(root))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]


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
    parser.add_argument("--debug", action="append", default=[], metavar="KEY=VALUE",
                        help="zusätzliche Zeile im [debug]-Abschnitt, z. B. perf=1 (mehrfach angebbar)")
    parser.add_argument("--cover-server", action="store_true",
                        help="lokalen Cover-Server starten und „Fehlende Cover laden“ nach NL_READY auslösen")
    args = parser.parse_args(argv)
    markers = args.markers or DEFAULT_MARKERS

    for name, path in (("Dolphin", args.dolphin), ("DOL", args.dol)):
        if not path.is_file():
            print(f"SMOKE FEHLER: {name} nicht gefunden: {path}")
            return 2

    server = None
    server_dir = None
    debug_lines = []
    if args.autostart:
        debug_lines.append(f"autostart = {args.autostart}")
    for item in args.debug:
        key, sep, value = item.partition("=")
        if not sep or not key.strip():
            print(f"SMOKE FEHLER: --debug erwartet KEY=VALUE: {item}")
            return 2
        debug_lines.append(f"{key.strip()} = {value.strip()}")
    if args.cover_server:
        server_dir = tempfile.TemporaryDirectory(prefix="nl_covers_")
        write_cover_server_files(Path(server_dir.name))
        server, port = start_cover_server(Path(server_dir.name))
        debug_lines += [f"cover_url = http://127.0.0.1:{port}", "fetch_covers = 1"]
    try:
        return _run(args, markers, debug_lines)
    finally:
        if server is not None:
            server.shutdown()
            server.server_close()
        if server_dir is not None:
            server_dir.cleanup()


def _run(args: argparse.Namespace, markers: list[str], debug_lines: list[str]) -> int:
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
        if debug_lines:
            with (sd / "newlauncher/config.ini").open("a", encoding="utf-8", newline="\n") as f:
                f.write("[debug]\n" + "".join(line + "\n" for line in debug_lines))
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
