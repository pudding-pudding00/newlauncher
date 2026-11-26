"""Smoke-Test: startet die DOL in Dolphin und wartet auf Marker im Log (Standard: NL_READY).

Exit-Codes: 0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt.
"""

import argparse
import functools
import http.server
import re
import ssl
import subprocess
import sys
import tempfile
import shutil
import threading
import time
import zipfile
import zlib
from pathlib import Path

from deploy_dolphin import ARGV_ECHO_DOL, DEFAULT_DOL, DIST_DIR, FIXTURES_DIR, deploy
from fat_frag import NotFat32Error, expected_frag_line, read_file
import save_smoke
from make_disk_image import make_image
from make_fixtures import disc_header
from dolphin import (DEFAULT_DOLPHIN, DEFAULT_USER_DIR, dolphin_command, log_path, reset_log,
                     sd_sync_dir, wait_for_markers, write_dev_config)
from pngfile import solid_png, write_png
from test_ca import make as make_test_ca
from wii_remote import REMOTE_PORT, RemoteClient, RemoteError, yuyv_to_rgba
from wii_title import TEST_CHANNEL_ID, TEST_CHANNEL_NAMES, install_channel

DEFAULT_MARKERS = ["NL_READY"]

# --expect-sysinfo (M13): die Datei zum Weitergeben darf keine MAC- und keine IPv4-Adresse enthalten.
SYSINFO_REL = "newlauncher/sysinfo.txt"
CONFIG_REL = "newlauncher/config.ini"
GAMES_REL = "newlauncher/games.ini"  # Spieloptionen pro Spiel (M14)
THEMES_REL = "newlauncher/themes"  # Theme-Pakete (M17)
CATEGORIES_REL = "newlauncher/categories.ini"  # eigene Kategorien (M17)
STATS_REL = "newlauncher/stats.ini"  # Spielstatistik (M17)
# Zwei gültige Tokens und ein kaputter (ohne #): der Launcher meldet genau eine NL_THEME_BAD-Zeile.
SMOKE_THEME_INI = "[light]\nbar = #123456\naccent = #ABCDEF\nbar_border = fff\n"
DEBUG_INI_REL = "newlauncher/debug.ini"  # [debug] ohne config.ini (--fresh-data, M13)
_MAC_RE = re.compile(r"[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}")
_IPV4_RE = re.compile(r"\b\d{1,3}(\.\d{1,3}){3}\b")


def sysinfo_problems(text: str) -> list[str]:
    """Leere Liste, wenn sysinfo.txt eine Plattformzeile und weder MAC noch IPv4 enthält."""
    problems = []
    if "Plattform: " not in text and "Platform: " not in text:
        problems.append("sysinfo.txt ohne Plattformzeile")
    if _MAC_RE.search(text):
        problems.append("sysinfo.txt enthält eine MAC-Adresse")
    if _IPV4_RE.search(text):
        problems.append("sysinfo.txt enthält eine IPv4-Adresse")
    return problems

def _norm_ini_line(line: str) -> str:
    if "=" in line:
        k, v = line.split("=", 1)
        return f"{k.strip()} = {v.strip()}"
    return line.strip()


def missing_ini_lines(text: str, expected: list[str]) -> list[str]:
    """Erwartete Zeilen, die in text fehlen (Leerraum um „=“ und am Rand zählt nicht)."""
    have = {_norm_ini_line(line) for line in text.splitlines()}
    return [e for e in expected if _norm_ini_line(e) not in have]

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
                      "US/GM8P01.png": (160, 224, (40, 40, 200, 255)),
                      # M22: Retro-Boxart im libretro-thumbnails-Schema (cover_url ersetzt auch die Retro-Basis)
                      "Nintendo_-_Nintendo_Entertainment_System/master/Named_Boxarts/Zelda (Europe) (Rev 1).png":
                          (160, 224, (40, 200, 40, 255)),
                      # M24: Git-Symlink wie bei raw.githubusercontent (Text mit dem Zielnamen) …
                      "Nintendo_-_Nintendo_64/master/Named_Boxarts/Super Mario 64 (Europe) (En,Fr,De).png":
                          b"Super Mario 64 (Europe).png",
                      "Nintendo_-_Nintendo_64/master/Named_Boxarts/Super Mario 64 (Europe).png":
                          (160, 224, (200, 200, 40, 255)),
                      # … und ein abweichender Name: libretro ergänzt „(En)“
                      "Nintendo_-_Super_Nintendo_Entertainment_System/master/Named_Boxarts/F-Zero (Europe) (En).png":
                          (160, 224, (200, 40, 200, 255))}


def write_cover_server_files(root: Path) -> None:
    for rel, spec in COVER_SERVER_FILES.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(spec if isinstance(spec, bytes) else solid_png(*spec))


# Selbst erzeugte Gecko-Codeliste (M15); "html" simuliert eine Fehlerseite statt einer Liste.
CHEAT_SERVER_ID6 = "RMCP01"
CHEAT_TXT_REL = f"newlauncher/codes/{CHEAT_SERVER_ID6}.txt"
SD_SETTLE_SECONDS = 3.0  # Wartezeit nach dem Marker, bis Dolphin die SD-Schreibzugriffe abgelegt hat


def write_cheat_server_files(root: Path, mode: str) -> None:
    path = root / "codes" / f"{CHEAT_SERVER_ID6}.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    if mode == "html":
        path.write_bytes(b"<!DOCTYPE html>\n<html><body>Fehler</body></html>\n")
    else:
        path.write_bytes(f"{CHEAT_SERVER_ID6}\nTestliste\n\nTestcode [Smoke]\n04001234 00000063\n".encode())


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


UPDATE_SERVER_VERSION = "99.0.0"


# M20: Rückkehr-Stub. Erster Lauf startet die ROM, argv_echo ruft exit(); der Stub lädt newlauncher von der SD,
# der zweite Lauf meldet den Stub und überspringt den Autostart.
RETURN_STUB_ROM = "gb/Tetris (World) (Rev 1).gb"
RETURN_STUB_MARKERS = ["NL_RETURN_STUB installed", "NL_ARGV 3 --exit", "NL_ARGV exit",
                       "NL_RETURN_STUB present stage=0", "NL_AUTOSTART skip return_stub"]


def write_update_server_files(root: Path, dol: Path) -> None:
    """M20: rel/update.ini und rel/newlauncher-<v>.zip wie aus make_release.py; boot.dol ist argv_echo, damit
    der Neustart nach dem Update sichtbar in der neuen DOL landet."""
    rel = root / "rel"
    rel.mkdir(parents=True, exist_ok=True)
    zip_path = rel / f"newlauncher-{UPDATE_SERVER_VERSION}.zip"
    entries = {"apps/newlauncher/boot.dol": dol.read_bytes(),
               "apps/newlauncher/meta.xml": b"<app version=\"1\"><name>newlauncher</name></app>\n"}
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries.items():
            z.writestr(name, data)
    unzipped = sum(len(d) for d in entries.values())
    lines = ["[update]", f"version = {UPDATE_SERVER_VERSION}", f"zip = {zip_path.name}",
             f"zip_size = {zip_path.stat().st_size}", f"unzipped_size = {unzipped}", ""]
    (rel / "update.ini").write_bytes("\n".join(lines).encode("utf-8"))


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
    parser.add_argument("--cover-auto", action="store_true",
                        help="M21: mit --cover-server ohne fetch_covers, aber [covers] auto = 1 (Autostart)")
    parser.add_argument("--emu-server", action="store_true",
                        help="lokales OSC-Test-Repository starten und fceugx nach NL_READY installieren")
    parser.add_argument("--expect-frag", metavar="REL_PATH",
                        help="NL_FRAG-Zeile für diese ISO (relativ zur SD) aus Load/WiiSD.raw berechnen und im Log verlangen")
    parser.add_argument("--cheat-server", choices=["ok", "html"],
                        help="Gecko-Codeliste (ok) oder HTML-Fehlerseite (html) ausliefern und nach NL_READY laden")
    parser.add_argument("--tls", action="store_true",
                        help="Server per HTTPS auf localhost mit einer Test-CA (braucht --cover-server/--emu-server)")
    parser.add_argument("--tls-wrong-ca", action="store_true",
                        help="wie --tls, aber der Launcher bekommt eine fremde CA (Gegenprobe: Zertifikatsfehler)")
    parser.add_argument("--detect-emus", action="store_true",
                        help="ohne emulators.ini: argv_echo als apps/fceugx/boot.dol mit meta.xml (Erkennung, M13)")
    parser.add_argument("--expect-sysinfo", action="store_true",
                        help="[debug] sysinfo_save = 1; danach sysinfo.txt aus der SD auf MAC/IP prüfen (M13)")
    parser.add_argument("--fresh-data", action="store_true",
                        help="ohne config.ini: [debug] in debug.ini mit wizard = accept; verlangt NL_WIZARD done, "
                             "config.ini im SD-Image und einen zweiten Start ohne Assistent (M13)")
    parser.add_argument("--sd-layout", choices=["default", "multi"], default="default",
                        help="multi: zusätzlicher NES-Ordner emu/nes mit [paths] und ein ZIP-ROM (M12)")
    parser.add_argument("--games-ini", metavar="TEXT",
                        help="Inhalt für newlauncher/games.ini (Spieloptionen, M14); ein wörtliches \\n trennt Zeilen. "
                             "Ohne die Option wird eine alte games.ini entfernt")
    parser.add_argument("--expect-games-ini", metavar="LINE", action="append", default=[],
                        help="Zeile, die nach dem Lauf in newlauncher/games.ini im SD-Image stehen muss (M14, mehrfach)")
    parser.add_argument("--config-append", metavar="TEXT",
                        help="Text, der vor dem [debug]-Block an config.ini angehängt wird (M14); \\n trennt Zeilen")
    parser.add_argument("--sd-disk", metavar="LAYOUT",
                        help="SD als Platte (M16), z. B. fat32,ntfs: baut ein Image mit MBR; die erste Partition "
                             "(fat32) ist der SD-Inhalt, weitere enthalten nl_ntfs.txt. Braucht Docker")
    parser.add_argument("--ntfs-iso", metavar="ID6:GRÖSSE", action="append", default=[],
                        help="mit --sd-disk: selbst erzeugtes Wii-Test-ISO auf die erste NTFS-Partition "
                             "(wbfs/Test [ID6]/ID6.iso), Größe in Bytes oder mit M, Vielfaches von 512")
    parser.add_argument("--wbfs-iso", metavar="ID6:GRÖSSE[:broken]", action="append", default=[],
                        help="mit --sd-disk …,wbfs: Test-Disc in einen Slot der ersten WBFS-Partition (Reihenfolge = "
                             "Slot); :broken ohne Wii-Magic in der Disc-Kopf-Kopie (M16 Task 5)")
    parser.add_argument("--expect-stats", metavar="LINE", action="append", default=[],
                        help="Zeile, die nach dem Lauf in newlauncher/stats.ini im SD-Image stehen muss (M17, mehrfach); "
                             "eine alte stats.ini wird vor jedem Lauf entfernt")
    parser.add_argument("--categories", metavar="TEXT",
                        help="Inhalt für newlauncher/categories.ini (M17); ein wörtliches \\n trennt Zeilen. "
                             "Ohne die Option wird eine alte categories.ini entfernt")
    parser.add_argument("--theme-pack", metavar="NAME",
                        help="Fixture-Theme newlauncher/themes/NAME mit einem kaputten Token anlegen und in config.ini "
                             "wählen; verlangt genau eine NL_THEME_BAD-Zeile (M17). Ohne die Option wird themes/ entfernt")
    parser.add_argument("--update-server", action="store_true",
                        help="M20: Selbst-Update von einem lokalen Server; A per Fernsteuerung, dann startet die "
                             "neue DOL (argv_echo)")
    parser.add_argument("--remote-check", action="store_true",
                        help="Fernsteuerung an, dann über 127.0.0.1:4300 HELLO/GAMES/KEY/STATE/SHOT/PUT/LOG prüfen (M20)")
    parser.add_argument("--return-stub", action="store_true",
                        help="M20: GB-ROM mit argv_echo --exit starten; dessen exit() muss über den Rückkehr-Stub "
                             "newlauncher neu laden")
    parser.add_argument("--emunand", action="store_true",
                        help="Fixture-EmuNAND sd:/emunand mit Testkanal 000100014E4C5443 anlegen (M19)")
    parser.add_argument("--save-fixture", choices=save_smoke.FIXTURES,
                        help="M26: Spielstand-Einheit mit Testbytes anlegen (gb: SD, nand: Dolphin-NAND, "
                             "emunand: sd:/emunand mit save = emu)")
    parser.add_argument("--save-step", choices=save_smoke.STEPS, default="backup",
                        help="M26: backup sichert und prüft das ZIP, restore spielt es zurück, recover rollt ein "
                             "Journal zurück (restore/recover brauchen einen backup-Lauf davor)")
    args = parser.parse_args(argv)
    if "--save-step" in (argv if argv is not None else sys.argv[1:]) and not args.save_fixture:
        parser.error("--save-step braucht --save-fixture")
    tls = args.tls or args.tls_wrong_ca
    if tls and not (args.cover_server or args.emu_server or args.cheat_server or args.update_server):
        parser.error("--tls/--tls-wrong-ca braucht --cover-server, --emu-server oder --cheat-server")
    if args.cover_auto and not args.cover_server:
        parser.error("--cover-auto braucht --cover-server")
    markers = args.markers or DEFAULT_MARKERS
    if args.remote_check:
        args.debug.append("remote=1")
        markers = markers + ["NL_REMOTE listen"]
    args.sd_disk_layout = None
    if args.sd_disk:
        layout = [k.strip() for k in args.sd_disk.split(",")]
        if layout[0] != "fat32" or any(k not in SD_DISK_KINDS for k in layout) or len(layout) > 4:
            print(f"SMOKE FEHLER: --sd-disk braucht fat32 zuerst, dann {', '.join(SD_DISK_KINDS)}: {args.sd_disk}")
            return 2
        args.sd_disk_layout = layout
        markers = markers + sd_disk_markers(layout)
    args.ntfs_isos = []
    for spec in args.ntfs_iso:
        iso = parse_ntfs_iso(spec)
        if iso is None or not args.sd_disk_layout or "ntfs" not in args.sd_disk_layout:
            print(f"SMOKE FEHLER: --ntfs-iso braucht ID6:GRÖSSE (Vielfaches von 512) und --sd-disk mit ntfs: {spec}")
            return 2
        args.ntfs_isos.append(iso)
    args.wbfs_isos = []
    for spec in args.wbfs_iso:
        base, _, flag = spec.partition(":")[2].partition(":")
        iso = parse_ntfs_iso(spec.partition(":")[0] + ":" + base)
        if iso is None or flag not in ("", "broken") or not args.sd_disk_layout or "wbfs" not in args.sd_disk_layout:
            print(f"SMOKE FEHLER: --wbfs-iso braucht ID6:GRÖSSE[:broken] und --sd-disk mit wbfs: {spec}")
            return 2
        args.wbfs_isos.append((iso[0], iso[1], flag == "broken"))

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
        debug_lines.append(f"cover_url = {scheme_host}:{port}")
        if not args.cover_auto:
            debug_lines.append("fetch_covers = 1")
    if args.emu_server:
        if not ARGV_ECHO_DOL.is_file():
            print(f"SMOKE FEHLER: argv_echo.dol nicht gebaut: {ARGV_ECHO_DOL}")
            return 2
        server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_emus_"))
        write_emu_server_files(Path(server_dirs[-1].name), ARGV_ECHO_DOL)
        server, port = start_cover_server(Path(server_dirs[-1].name), ssl_context)
        servers.append(server)
        debug_lines += [f"emu_repo = {scheme_host}:{port}/hbb", f"emu_install = {EMU_SERVER_APP}"]
    if args.update_server:
        if not ARGV_ECHO_DOL.is_file():
            print(f"SMOKE FEHLER: argv_echo.dol nicht gebaut: {ARGV_ECHO_DOL}")
            return 2
        server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_update_"))
        write_update_server_files(Path(server_dirs[-1].name), ARGV_ECHO_DOL)
        server, port = start_cover_server(Path(server_dirs[-1].name), ssl_context)
        servers.append(server)
        debug_lines += [f"update_url = {scheme_host}:{port}/rel/update.ini", "self_update = 1", "remote = 1"]
        markers = markers + [f"NL_SELF_UPDATE ok {UPDATE_SERVER_VERSION}", "NL_REMOTE listen"]
    if args.cheat_server:
        server_dirs.append(tempfile.TemporaryDirectory(prefix="nl_cheats_"))
        write_cheat_server_files(Path(server_dirs[-1].name), args.cheat_server)
        server, port = start_cover_server(Path(server_dirs[-1].name), ssl_context)
        servers.append(server)
        debug_lines += [f"cheat_url = {scheme_host}:{port}/codes/{{id6}}.txt", f"cheat_fetch = {CHEAT_SERVER_ID6}"]
        markers = markers + ["NL_CHEAT_FETCH " + ("ok" if args.cheat_server == "ok" else "nocodes")]
    if getattr(args, "return_stub", False):
        debug_lines.append(f"autostart = {RETURN_STUB_ROM}")
        markers = markers + RETURN_STUB_MARKERS
    if args.expect_sysinfo:
        debug_lines.append("sysinfo_save = 1")
        markers = markers + ["NL_SYSINFO_SAVED"]
    if args.fresh_data:
        markers = markers + ["NL_WIZARD done"]
        if not args.cover_server:  # M21: Cover-Autostart nach dem Assistenten nicht ins Internet
            debug_lines.append("cover_url = http://127.0.0.1:9")
    if args.sd_disk_layout:
        debug_lines.append("sd_as_disk = 1")
    debug_lines += debug_lines_tls
    try:
        return _run(args, markers, debug_lines)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for server_dir in server_dirs:
            server_dir.cleanup()


SD_DISK_KINDS = ("fat32", "ntfs", "wbfs")
SD_DISK_PROBE = "nl_ntfs.txt"  # Inhalt der weiteren Partitionen: eine Datei, die sich lesen lässt


def sd_disk_markers(layout: list[str]) -> list[str]:
    """Erwartete NL_DEV-Zeilen: Partitionen in MBR-Reihenfolge als usb, usb2, …; NTFS und WBFS nur lesend (M16)."""
    names = {"fat32": "fat", "ntfs": "ntfs", "wbfs": "wbfs"}
    out = []
    for i, kind in enumerate(layout):
        name = "usb" if i == 0 else f"usb{i + 1}"
        out.append(f"NL_DEV {name} {names[kind]} mounted" + (" ro" if kind in ("ntfs", "wbfs") else ""))
    return out


def parse_ntfs_iso(spec: str) -> tuple[str, int] | None:
    """ID6:GRÖSSE → (ID6, Bytes); None bei falscher ID oder Größe, die kein Vielfaches von 512 ist."""
    id6, _, size = spec.partition(":")
    if not re.fullmatch(r"[A-Z0-9]{6}", id6) or not re.fullmatch(r"\d+M?", size):
        return None
    n = int(size.rstrip("M")) * (1 << 20 if size.endswith("M") else 1)
    return (id6, n) if n > 0 and n % 512 == 0 else None


def _write_test_iso(root: Path, id6: str, size: int, path: Path | None = None, title: str = "NTFS Test") -> None:
    """Wii-Kopf plus Füllmuster ohne Null-Sektoren, damit cp/ntfs-3g keine Löcher (sparse) anlegen."""
    path = path or root / "wbfs" / f"Test [{id6}]" / f"{id6}.iso"
    path.parent.mkdir(parents=True, exist_ok=True)
    head = bytes(disc_header(id6, f"{title} {id6}", wii=True))[:512]
    fill = bytes(((i * 7) & 0xFF) or 1 for i in range(1 << 16))
    with path.open("wb") as f:
        f.write(head)
        left = size - len(head)
        while left > 0:
            n = min(left, len(fill))
            f.write(fill[:n])
            left -= n


def _build_sd_disk(layout: list[str], sd: Path, out: Path, isos: list[tuple[str, int]] | None = None,
                   wbfs_isos: list[tuple[str, int, bool]] | None = None) -> int:
    with tempfile.TemporaryDirectory(prefix="nl_sddisk_") as tmp:
        probe = Path(tmp) / "probe"
        probe.mkdir()
        (probe / SD_DISK_PROBE).write_text("newlauncher M16\n", encoding="utf-8", newline="\n")
        specs = [("fat32", sd)] + [(kind, probe) for kind in layout[1:]]
        if isos:
            # Nur die erste NTFS-Partition bekommt die Test-ISOs; weitere bleiben reine Proben.
            games = Path(tmp) / "games"
            shutil.copytree(probe, games)
            for id6, size in isos:
                _write_test_iso(games, id6, size)
            specs[layout.index("ntfs")] = ("ntfs", games)
        if wbfs_isos:
            # Erste WBFS-Partition: NN_ID6[.broken].iso, make_disk_image vergibt die Slots in Namensreihenfolge.
            discs = Path(tmp) / "wbfs_discs"
            discs.mkdir()
            for n, (id6, size, broken) in enumerate(wbfs_isos):
                name = f"{n:02d}_{id6}" + (".broken" if broken else "") + ".iso"
                _write_test_iso(discs, id6, size, discs / name, "WBFS Test")
            specs[layout.index("wbfs")] = ("wbfs", discs)
        rc = make_image(out, specs)
    if rc != 0:
        print(f"SMOKE FEHLER: SD-Image nicht gebaut (rc={rc})")
    return rc


def _cli_text(text: str) -> str:
    """Kommandozeilentext: wörtliches \\n wird zum Zeilenumbruch; endet immer mit einem Zeilenumbruch."""
    text = text.replace("\\n", "\n")
    return text if text.endswith("\n") else text + "\n"


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
        if getattr(args, "return_stub", False):
            set_ini_value(sd / "newlauncher/emulators.ini", "gb", "args", "{dir}|{file}|--exit")
        if getattr(args, "cheat_server", None):
            (sd / CHEAT_TXT_REL).unlink(missing_ok=True)  # sonst endet der Lauf mit „exists“
        if getattr(args, "sd_ca", None) is not None:
            shutil.copyfile(args.sd_ca, sd / SD_TEST_CA)
        if args.sd_layout == "multi":
            write_multi_layout(sd)
        (sd / SYSINFO_REL).unlink(missing_ok=True)  # alte Datei darf die Prüfung nicht bestehen lassen
        (sd / DEBUG_INI_REL).unlink(missing_ok=True)  # Rest eines --fresh-data-Laufs
        (sd / (CONFIG_REL + ".bak")).unlink(missing_ok=True)
        if args.games_ini is not None:
            (sd / GAMES_REL).write_text(_cli_text(args.games_ini), encoding="utf-8", newline="\n")
        else:
            (sd / GAMES_REL).unlink(missing_ok=True)  # Optionen eines früheren Laufs dürfen nicht nachwirken
        if getattr(args, "categories", None) is not None:
            (sd / CATEGORIES_REL).write_text(_cli_text(args.categories), encoding="utf-8", newline="\n")
        else:
            (sd / CATEGORIES_REL).unlink(missing_ok=True)  # Kategorien eines früheren Laufs dürfen nicht nachwirken
        (sd / STATS_REL).unlink(missing_ok=True)  # Starts eines früheren Laufs dürfen nicht mitzählen
        shutil.rmtree(sd / THEMES_REL, ignore_errors=True)  # Theme eines früheren Laufs darf nicht nachwirken
        if getattr(args, "theme_pack", None):
            theme_dir = sd / THEMES_REL / args.theme_pack
            theme_dir.mkdir(parents=True)
            (theme_dir / "theme.ini").write_text(SMOKE_THEME_INI, encoding="utf-8", newline="\n")
            if not args.fresh_data:
                with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                    f.write(f"[theme]\nname = {args.theme_pack}\n")
        if not args.fresh_data and (sd / CONFIG_REL).is_file():
            # Fixture-config.ini hat [covers] auto = 0 (M21), damit Smokes nicht von selbst laden. Auch mit
            # --no-fixtures zurücksetzen: ein früherer --cover-auto-Lauf hinterlässt sonst auto = 1.
            cfg = sd / CONFIG_REL
            on = getattr(args, "cover_auto", False)
            old, new = ("\nauto = 0\n", "\nauto = 1\n") if on else ("\nauto = 1\n", "\nauto = 0\n")
            text = cfg.read_text(encoding="utf-8")
            cfg.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
        if args.config_append and not args.fresh_data:
            with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                f.write(_cli_text(args.config_append))
        save_fx = None
        if getattr(args, "save_fixture", None):
            save_fx = save_smoke.fixture(args.save_fixture, user_dir)
            try:
                plan = save_smoke.prepare(save_fx, args.save_step, user_dir)
            except (FileNotFoundError, ValueError) as e:
                save_smoke.cleanup(save_fx, user_dir)
                print(f"SMOKE FEHLER: Spielstand-Fixture: {e}")
                return 2
            args.emunand = args.emunand or plan.emunand
            debug_lines = debug_lines + plan.debug_lines
            markers = markers + plan.markers
            if plan.config_append and not args.fresh_data:
                with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                    f.write(plan.config_append)
        args.save_fx = save_fx
        if getattr(args, "emunand", False):
            if not ARGV_ECHO_DOL.is_file():
                print(f"SMOKE FEHLER: argv_echo.dol nicht gebaut: {ARGV_ECHO_DOL}")
                return 2
            install_channel(sd / "emunand", TEST_CHANNEL_ID, TEST_CHANNEL_NAMES, ARGV_ECHO_DOL.read_bytes())
            if not args.fresh_data:
                with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                    f.write("[emunand]\npath = sd:/emunand\n")
        if getattr(args, "sd_disk_layout", None) and not args.fresh_data:
            # Fixture-config.ini sagt library = sd; mit „auto“ fragt der Launcher die Platte (die SD) ab.
            with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                f.write("[library]\nsources = auto\n")
        if args.fresh_data:
            # Erster Start (Spec v2 6.4): keine config.ini; [debug] kommt aus debug.ini.
            (sd / CONFIG_REL).unlink(missing_ok=True)
            with (sd / DEBUG_INI_REL).open("w", encoding="utf-8", newline="\n") as f:
                f.write("[debug]\nwizard = accept\n" + "".join(line + "\n" for line in debug_lines))
        elif debug_lines:
            with (sd / CONFIG_REL).open("a", encoding="utf-8", newline="\n") as f:
                f.write("[debug]\n" + "".join(line + "\n" for line in debug_lines))
        reset_log(user_dir)
    except OSError as e:
        print(f"SMOKE FEHLER: Dev-Ordner nicht vorbereitet (läuft Dolphin noch?): {e}")
        return 2

    args.sd_image = None
    if getattr(args, "sd_disk_layout", None):
        if _build_sd_disk(args.sd_disk_layout, sd, user_dir / "sd_disk.raw", args.ntfs_isos,
                          getattr(args, "wbfs_isos", None)) != 0:
            return 2
        args.sd_image = user_dir / "sd_disk.raw"
    try:
        return _run_dolphin(args, markers, user_dir)
    finally:
        (sd_sync_dir(user_dir) / SD_TEST_CA).unlink(missing_ok=True)  # Test-CA nicht auf der SD lassen
        shutil.rmtree(sd_sync_dir(user_dir) / MULTI_DIR, ignore_errors=True)  # --sd-layout multi
        shutil.rmtree(sd_sync_dir(user_dir) / "emunand", ignore_errors=True)  # --emunand
        if getattr(args, "save_fx", None) is not None:
            save_smoke.cleanup(args.save_fx, user_dir)


def _run_dolphin(args: argparse.Namespace, markers: list[str], user_dir: Path) -> int:
    try:
        proc = subprocess.Popen(dolphin_command(args.dolphin, user_dir, args.dol.resolve(),
                                                getattr(args, "sd_image", None),
                                                cheats=getattr(args, "return_stub", False)))
    except OSError as e:
        print(f"SMOKE FEHLER: Dolphin startet nicht: {e}")
        return 2
    try:
        missing = wait_for_markers(log_path(user_dir), markers, args.timeout,
                                   is_alive=lambda: proc.poll() is None)
        if not missing and getattr(args, "remote_check", False):
            missing = _remote_session(user_dir)
        if not missing and getattr(args, "update_server", False):
            missing = _update_restart(user_dir)
        if not missing and (getattr(args, "cheat_server", None) or getattr(args, "save_fx", None)):
            # Dolphin puffert SD-Schreibzugriffe; ein sofortiges Beenden verliert die gerade geschriebene Datei.
            time.sleep(SD_SETTLE_SECONDS)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    if not missing and args.expect_frag:
        missing = _check_frag(user_dir, args.expect_frag)
    if not missing and args.expect_sysinfo:
        missing = _check_sysinfo(user_dir)
    if not missing and getattr(args, "expect_games_ini", None):
        missing = _check_games_ini(user_dir, args.expect_games_ini)
    if not missing and getattr(args, "expect_stats", None):
        missing = _check_sd_ini(user_dir, STATS_REL, args.expect_stats)
    if not missing and getattr(args, "theme_pack", None):
        missing = _check_theme_bad(user_dir)
    if not missing and getattr(args, "cheat_server", None):
        missing = _check_cheat_file(user_dir, args.cheat_server == "ok")
    if not missing and getattr(args, "fresh_data", False):
        missing = _check_second_start(args, user_dir)
    if not missing and getattr(args, "save_fx", None) is not None:
        missing = save_smoke.post_check(args.save_fx, args.save_step, user_dir, _read_log_text(user_dir),
                                        lambda rel: read_file(user_dir / "Load/WiiSD.raw", rel))
    if not missing:
        print("SMOKE OK")
        return 0
    print(f"SMOKE FEHLGESCHLAGEN: fehlende Marker nach {args.timeout:g} s: {', '.join(missing)}")
    return 1


def _read_log_text(user_dir: Path) -> str:
    try:
        return log_path(user_dir).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _update_restart(user_dir: Path) -> list[str]:
    """M20: Der Dialog „Version … ist installiert“ steht; A schließt ihn, newlauncher startet die neue boot.dol
    von der SD (hier argv_echo) mit --remote."""
    try:
        with RemoteClient("127.0.0.1", REMOTE_PORT, timeout=20) as c:
            c.call("KEY a")
    except (OSError, RemoteError) as e:
        return [f"UPDATE KEY a ({e})"]
    log = log_path(user_dir)
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        text = log.read_text(encoding="utf-8", errors="replace")
        if "NL_ARGV 1 --remote" in text and "NL_RESTART sd:/apps/newlauncher/boot.dol" in text:
            return []
        time.sleep(0.5)
    return ["NL_RESTART + NL_ARGV 1 --remote (neue DOL nach dem Update)"]


def _remote_session(user_dir: Path) -> list[str]:
    """M20: eine Sitzung gegen den Fernsteuerungs-Server; Dolphin reicht Wii-Sockets an den PC durch."""
    try:
        with RemoteClient("127.0.0.1", REMOTE_PORT, timeout=20) as c:
            if not c.call("HELLO")[0].startswith("newlauncher "):
                return ["REMOTE HELLO"]
            games = [ln for ln in c.call("GAMES")[1].decode().splitlines() if ln]
            if not games or any(ln.count("\t") != 2 for ln in games):
                return ["REMOTE GAMES"]
            state = dict(ln.split("\t", 1) for ln in c.call("STATE")[1].decode().splitlines() if "\t" in ln)
            c.call("KEY right")
            moved = dict(ln.split("\t", 1) for ln in c.call("STATE")[1].decode().splitlines() if "\t" in ln)
            if state.get("focus") == moved.get("focus"):
                return [f"REMOTE KEY (Fokus blieb {state.get('focus')})"]
            info, shot = c.call("SHOT")
            w, h, fmt = info.split(" ")
            # Nur Format und Größe: Dolphin hält den XFB nicht im emulierten RAM (Inhalt: Hardware-Abnahme).
            if fmt != "yuyv" or len(shot) != int(w) * int(h) * 2:
                return [f"REMOTE SHOT {info} {len(shot)}"]
            write_png(user_dir / "remote_shot.png", int(w), int(h), yuyv_to_rgba(shot, int(w), int(h)))
            target = state["data"] + "/remote_put.txt"
            c.put(b"remote\n", target)
            one = b"\x01"
            if (c.request(f"PUT 1 {zlib.crc32(one):08x} sd:/wbfs/x", one)[0]
                    or c.request(f"PUT 1 {zlib.crc32(b'x'):08x} {target}", one)[0]):
                return ["REMOTE PUT (verbotener Pfad oder falsche CRC angenommen)"]
            if f"NL_REMOTE put {target} bytes=7 ok" not in c.call("LOG")[1].decode("utf-8", "replace"):
                return ["REMOTE LOG"]
            restarted = c.call("RESTART")[0]
        # Die neue Instanz startet mit --remote und lauscht wieder: zweite listen-Zeile, dann HELLO.
        log = log_path(user_dir)
        deadline = time.monotonic() + 45
        while log.read_text(encoding="utf-8", errors="replace").count("NL_REMOTE listen") < 2:
            if time.monotonic() > deadline:
                return [f"REMOTE RESTART ({restarted}) lauscht nicht wieder"]
            time.sleep(0.5)
        with RemoteClient("127.0.0.1", REMOTE_PORT, timeout=20) as c:
            if not c.call("HELLO")[0].startswith("newlauncher "):
                return ["REMOTE HELLO nach RESTART"]
    except (RemoteError, OSError, KeyError, ValueError) as e:
        return [f"REMOTE {e}"]
    print(f"REMOTE OK: {len(games)} Spiele, Fokus {state.get('focus')} -> {moved.get('focus')}, Bild {w}x{h}")
    return []


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


def _check_cheat_file(user_dir: Path, expected: bool) -> list[str]:
    """Codeliste im SD-Image: bei „ok“ vorhanden und mit ID6, bei „html“ nicht angelegt."""
    try:
        text = read_file(user_dir / "Load/WiiSD.raw", CHEAT_TXT_REL)
    except NotFat32Error:
        print("SMOKE HINWEIS: SD-Image nicht FAT32, Codelisten-Prüfung übersprungen")
        return []
    except (OSError, ValueError):
        text = None
    if expected and (text is None or not text.startswith(CHEAT_SERVER_ID6.encode())):
        return [CHEAT_TXT_REL + " (fehlt)"]
    if not expected and text is not None:
        return [CHEAT_TXT_REL + " (darf nicht entstehen)"]
    return []


def _check_second_start(args: argparse.Namespace, user_dir: Path) -> list[str]:
    """config.ini aus dem SD-Image in den Sync-Ordner übernehmen und noch einmal starten: kein Assistent mehr."""
    try:
        text = read_file(user_dir / "Load/WiiSD.raw", CONFIG_REL)
    except (OSError, ValueError, NotFat32Error) as e:
        print(f"SMOKE FEHLER: config.ini nicht im SD-Image: {e}")
        return ["config.ini (SD)"]
    (sd_sync_dir(user_dir) / CONFIG_REL).write_bytes(text)
    reset_log(user_dir)
    try:
        proc = subprocess.Popen(dolphin_command(args.dolphin, user_dir, args.dol.resolve()))
    except OSError as e:
        print(f"SMOKE FEHLER: Dolphin startet nicht (zweiter Start): {e}")
        return ["zweiter Start"]
    try:
        missing = wait_for_markers(log_path(user_dir), ["NL_READY", "NL_CONFIG ok"], args.timeout,
                                   is_alive=lambda: proc.poll() is None)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    if missing:
        return [m + " (zweiter Start)" for m in missing]
    log = log_path(user_dir).read_text(encoding="utf-8", errors="replace")
    return ["kein NL_WIZARD im zweiten Start"] if "NL_WIZARD start" in log else []


def _check_sysinfo(user_dir: Path) -> list[str]:
    """sysinfo.txt aus dem SD-Image lesen (Dolphin wird beendet, die Ordner-Synchronisation ist nicht sicher)."""
    raw = user_dir / "Load/WiiSD.raw"
    try:
        text = read_file(raw, SYSINFO_REL).decode("utf-8", errors="replace")
    except (OSError, ValueError, NotFat32Error) as e:
        synced = sd_sync_dir(user_dir) / SYSINFO_REL
        if not synced.is_file():
            print(f"SMOKE FEHLER: sysinfo.txt nicht lesbar: {e}")
            return ["sysinfo.txt"]
        text = synced.read_text(encoding="utf-8", errors="replace")
    problems = sysinfo_problems(text)
    if not problems:
        print(f"SMOKE sysinfo.txt geprüft ({len(text.splitlines())} Zeilen)")
    return problems


def _check_theme_bad(user_dir: Path) -> list[str]:
    """Das Fixture-Theme hat genau einen kaputten Token; mehr oder weniger NL_THEME_BAD-Zeilen sind ein Fehler (M17)."""
    try:
        text = log_path(user_dir).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ["NL_THEME_BAD"]
    count = sum(1 for line in text.splitlines() if "NL_THEME_BAD " in line)
    if count == 1:
        print("SMOKE Theme geprüft (1 NL_THEME_BAD)")
        return []
    return [f"genau 1 NL_THEME_BAD (gefunden: {count})"]


def _check_games_ini(user_dir: Path, expected: list[str]) -> list[str]:
    """games.ini aus dem SD-Image lesen und die erwarteten Zeilen verlangen (M14)."""
    return _check_sd_ini(user_dir, GAMES_REL, expected)


def _check_sd_ini(user_dir: Path, rel: str, expected: list[str]) -> list[str]:
    """Eine INI-Datei aus dem SD-Image lesen und die erwarteten Zeilen verlangen (M14, M17)."""
    name = rel.rsplit("/", 1)[-1]
    try:
        text = read_file(user_dir / "Load/WiiSD.raw", rel).decode("utf-8", errors="replace")
    except (OSError, ValueError, NotFat32Error) as e:
        print(f"SMOKE FEHLER: {name} nicht lesbar: {e}")
        return [name]
    absent = missing_ini_lines(text, expected)
    if not absent:
        print(f"SMOKE {name} geprüft ({len(expected)} Zeilen)")
    return [f"{name}: {line}" for line in absent]


if __name__ == "__main__":
    sys.exit(main())
