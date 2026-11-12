"""Smoke-Test: startet die DOL in Dolphin und wartet auf Marker im Log (Standard: NL_READY).

Exit-Codes: 0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt.
"""

import argparse
import functools
import http.server
import ssl
import subprocess
import sys
import tempfile
import shutil
import threading
import zipfile
from pathlib import Path

from deploy_dolphin import ARGV_ECHO_DOL, DEFAULT_DOL, DIST_DIR, FIXTURES_DIR, deploy
from fat_frag import NotFat32Error, expected_frag_line
from dolphin import (DEFAULT_DOLPHIN, DEFAULT_USER_DIR, dolphin_command, log_path, reset_log,
                     sd_sync_dir, wait_for_markers, write_dev_config)
from pngfile import solid_png
from test_ca import make as make_test_ca

DEFAULT_MARKERS = ["NL_READY"]

# Bilder des Test-Cover-Servers (Pfad wie bei GameTDB: <Region>/<ID6>.png). RMGP01 fehlt absichtlich.
# --sd-layout multi (M12): eigener NES-Ordner neben roms/nes und ein ZIP-ROM. Nur zur Laufzeit erzeugt.
MULTI_DIR = "emu"
MULTI_ROM = "emu/nes/Kirby (Test).nes"
MULTI_ZIP = "emu/nes/Metroid (Test).zip"
MULTI_INI = "[paths]\nnes = roms/nes|emu/nes\n"  # .zip ist Standardendung (Spec v2 5.3)


def write_multi_layout(sd: Path) -> None:
    rom = b"NES\x1a" + bytes(16 * 1024 - 4)
    (sd / MULTI_ROM).parent.mkdir(parents=True, exist_ok=True)
    (sd / MULTI_ROM).write_bytes(rom)
    with zipfile.ZipFile(sd / MULTI_ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Metroid (Test).nes", rom)
    with (sd / "newlauncher/config.ini").open("a", encoding="utf-8", newline="\n") as f:
        f.write(MULTI_INI)


COVER_SERVER_FILES = {"EN/RMCP01.png": (160, 224, (200, 40, 40, 255)),
                      "US/GM8P01.png": (160, 224, (40, 40, 200, 255))}


def write_cover_server_files(root: Path) -> None:
    for rel, (w, h, color) in COVER_SERVER_FILES.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(solid_png(w, h, color))


# Test-Repository im OSC-Format (Spec 6.9): eine App fceugx, deren boot.dol die argv_echo-Test-DOL ist.
EMU_SERVER_APP = "fceugx"
EMU_SERVER_TIMESTAMP = 1793664000


def write_emu_server_files(root: Path, dol: Path) -> None:
    """Schreibt hbb/listv036.txt und hbb/fceugx/fceugx.zip; Größen in der Liste passen zum ZIP."""
    app_dir = root / "hbb" / EMU_SERVER_APP
    app_dir.mkdir(parents=True, exist_ok=True)
    zip_path = app_dir / f"{EMU_SERVER_APP}.zip"
    entries = {f"apps/{EMU_SERVER_APP}/boot.dol": dol.read_bytes(),
               f"apps/{EMU_SERVER_APP}/meta.xml": b"<app version=\"1\"><name>FCE Ultra GX</name>"
                                                   b"<version>9.9-test</version></app>\n",
               f"{EMU_SERVER_APP}/{EMU_SERVER_APP}.cfg": b"; Test-Einstellungen\n"}
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries.items():
            z.writestr(name, data)
    unzipped = sum(len(d) for d in entries.values())
    lines = ["Homebrew 2092896 v0.3.9e | - Testliste",
             "=Emulators=",
             f"{EMU_SERVER_APP} {EMU_SERVER_TIMESTAMP} 0 {unzipped} dol {zip_path.stat().st_size} 0 0 wwwwngcs "
             f"/{EMU_SERVER_APP};/apps/{EMU_SERVER_APP}; . .",
             "FCE Ultra GX", "newlauncher-Test", "9.9-test", str(unzipped), "Nintendo Emulator",
             "Test-Paket aus dolphin_smoke.py."]
    (root / "hbb/listv036.txt").write_bytes(("\r\n".join(lines) + "\r\n").encode("utf-8"))


def set_ini_value(path: Path, section: str, key: str, value: str) -> None:
    """Ersetzt key im Abschnitt section (Textform bleibt sonst unverändert)."""
    out, current, done = [], None, False
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            current = stripped[1:-1]
        elif current == section and stripped.partition("=")[0].strip() == key:
            line, done = f"{key} = {value}", True
        out.append(line)
    if not done:
        raise ValueError(f"[{section}] {key} fehlt in {path}")
    path.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):  # noqa: A002 - Signatur der Basisklasse
        pass


# Pfad der Test-CA auf der virtuellen SD ([debug] tls_ca); wird nach dem Lauf gelöscht.
SD_TEST_CA = "newlauncher/test_ca.pem"


def ssl_server_context(cert: Path, key: Path) -> ssl.SSLContext:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(cert), str(key))
    return ctx


def start_cover_server(root: Path, ssl_context: ssl.SSLContext | None = None
                       ) -> tuple[http.server.ThreadingHTTPServer, int]:
    """Startet einen HTTP(S)-Server für root auf 127.0.0.1 und einem freien Port, in einem Thread."""
    handler = functools.partial(_QuietHandler, directory=str(root))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    if ssl_context is not None:
        server.socket = ssl_context.wrap_socket(server.socket, server_side=True)
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
    parser.add_argument("--emu-server", action="store_true",
                        help="lokales OSC-Test-Repository starten und fceugx nach NL_READY installieren")
    parser.add_argument("--expect-frag", metavar="REL_PATH",
                        help="NL_FRAG-Zeile für diese ISO (relativ zur SD) aus Load/WiiSD.raw berechnen und im Log verlangen")
    parser.add_argument("--tls", action="store_true",
                        help="Server per HTTPS auf localhost mit einer Test-CA (braucht --cover-server/--emu-server)")
    parser.add_argument("--tls-wrong-ca", action="store_true",
                        help="wie --tls, aber der Launcher bekommt eine fremde CA (Gegenprobe: Zertifikatsfehler)")
    parser.add_argument("--detect-emus", action="store_true",
                        help="ohne emulators.ini: argv_echo als apps/fceugx/boot.dol mit meta.xml (Erkennung, M13)")
    parser.add_argument("--sd-layout", choices=["default", "multi"], default="default",
                        help="multi: zusätzlicher NES-Ordner emu/nes mit [paths] und ein ZIP-ROM (M12)")
    args = parser.parse_args(argv)
    tls = args.tls or args.tls_wrong_ca
    if tls and not (args.cover_server or args.emu_server):
        parser.error("--tls/--tls-wrong-ca braucht --cover-server oder --emu-server")
    markers = args.markers or DEFAULT_MARKERS

    for name, path in (("Dolphin", args.dolphin), ("DOL", args.dol)):
        if not path.is_file():
            print(f"SMOKE FEHLER: {name} nicht gefunden: {path}")
            return 2

    servers = []      # Cover- und Emulator-Server dürfen gemeinsam laufen (M11: eine Netz-Warteschlange)
    server_dirs = []
    ssl_context = None
    args.sd_ca = None
    scheme_host = "http://127.0.0.1"
    if tls:
        try:
            server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_ca_"))
            ca_root = Path(server_dirs[-1].name)
            server_ca = make_test_ca(ca_root / "server")
            ssl_context = ssl_server_context(server_ca["cert"], server_ca["key"])
            # Gegenprobe: eine CA, die das Serverzertifikat nicht unterschrieben hat.
            args.sd_ca = make_test_ca(ca_root / "fremd")["ca"] if args.tls_wrong_ca else server_ca["ca"]
        except Exception as e:  # noqa: BLE001 - openssl fehlt oder scheitert: Voraussetzung
            for d in server_dirs:
                d.cleanup()
            print(f"SMOKE FEHLER: Test-CA nicht erzeugt: {e}")
            return 2
        scheme_host = "https://localhost"
        debug_lines_tls = [f"tls_ca = sd:/{SD_TEST_CA}"]
    else:
        debug_lines_tls = []
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
        server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_covers_"))
        write_cover_server_files(Path(server_dirs[-1].name))
        server, port = start_cover_server(Path(server_dirs[-1].name), ssl_context)
        servers.append(server)
        debug_lines += [f"cover_url = {scheme_host}:{port}", "fetch_covers = 1"]
    if args.emu_server:
        if not ARGV_ECHO_DOL.is_file():
            print(f"SMOKE FEHLER: argv_echo.dol nicht gebaut: {ARGV_ECHO_DOL}")
            return 2
        server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_emus_"))
        write_emu_server_files(Path(server_dirs[-1].name), ARGV_ECHO_DOL)
        server, port = start_cover_server(Path(server_dirs[-1].name), ssl_context)
        servers.append(server)
        debug_lines += [f"emu_repo = {scheme_host}:{port}/hbb", f"emu_install = {EMU_SERVER_APP}"]
    debug_lines += debug_lines_tls
    try:
        return _run(args, markers, debug_lines)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for server_dir in server_dirs:
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
        if args.emu_server:
            # Der Launcher installiert fceugx selbst; NES startet danach über die installierte DOL.
            shutil.rmtree(sd / f"apps/{EMU_SERVER_APP}", ignore_errors=True)
            (sd / "newlauncher/emulators_installed.ini").unlink(missing_ok=True)
            set_ini_value(sd / "newlauncher/emulators.ini", "nes", "dol", f"sd:/apps/{EMU_SERVER_APP}/boot.dol")
        if args.detect_emus:
            # Erkennung statt emulators.ini: FCE Ultra GX liegt als argv_echo in apps/fceugx/ (Spec v2 6.3).
            if not ARGV_ECHO_DOL.is_file():
                print(f"SMOKE FEHLER: argv_echo.dol nicht gebaut: {ARGV_ECHO_DOL}")
                return 2
            (sd / "newlauncher/emulators.ini").unlink(missing_ok=True)
            (sd / f"apps/{EMU_SERVER_APP}").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ARGV_ECHO_DOL, sd / f"apps/{EMU_SERVER_APP}/boot.dol")
            (sd / f"apps/{EMU_SERVER_APP}/meta.xml").write_text(
                "<app><name>FCE Ultra GX</name></app>", encoding="utf-8")
        if getattr(args, "sd_ca", None) is not None:
            shutil.copyfile(args.sd_ca, sd / SD_TEST_CA)
        if args.sd_layout == "multi":
            write_multi_layout(sd)
        if debug_lines:
            with (sd / "newlauncher/config.ini").open("a", encoding="utf-8", newline="\n") as f:
                f.write("[debug]\n" + "".join(line + "\n" for line in debug_lines))
        reset_log(user_dir)
    except OSError as e:
        print(f"SMOKE FEHLER: Dev-Ordner nicht vorbereitet (läuft Dolphin noch?): {e}")
        return 2

    try:
        return _run_dolphin(args, markers, user_dir)
    finally:
        (sd_sync_dir(user_dir) / SD_TEST_CA).unlink(missing_ok=True)  # Test-CA nicht auf der SD lassen
        shutil.rmtree(sd_sync_dir(user_dir) / MULTI_DIR, ignore_errors=True)  # --sd-layout multi


def _run_dolphin(args: argparse.Namespace, markers: list[str], user_dir: Path) -> int:
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

    if not missing and args.expect_frag:
        missing = _check_frag(user_dir, args.expect_frag)
    if not missing:
        print("SMOKE OK")
        return 0
    print(f"SMOKE FEHLGESCHLAGEN: fehlende Marker nach {args.timeout:g} s: {', '.join(missing)}")
    return 1


def _check_frag(user_dir: Path, rel_path: str) -> list[str]:
    """Erwartete NL_FRAG-Zeile aus dem SD-Image; fehlt sie im Log, wird sie als fehlender Marker gemeldet."""
    try:
        line = expected_frag_line(user_dir / "Load/WiiSD.raw", rel_path)
    except NotFat32Error:
        print("SMOKE HINWEIS: SD-Image nicht FAT32, Abgleich übersprungen")
        return []
    except (OSError, ValueError) as e:
        print(f"SMOKE FEHLER: Abgleich NL_FRAG nicht möglich: {e}")
        return ["NL_FRAG (Abgleich)"]
    log = log_path(user_dir).read_text(encoding="utf-8", errors="replace")
    return [] if line in log else [line]


if __name__ == "__main__":
    sys.exit(main())
