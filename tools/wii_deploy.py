"""Bringt den Launcher auf eine echte SD-Karte oder per wiiload auf die Wii (M7, Hardware-Abnahme).

    python tools/wii_deploy.py E: [--init] [--perf on|off] [--remote on|off] [--dol PATH]
    python tools/wii_deploy.py E: --selftest [--dol PATH]
    python tools/wii_deploy.py --wiiload 192.168.0.42 [--dol PATH]
    python tools/wii_deploy.py E: --emulators [NAME ...] [--repo URL] [--yes]

Release: kopiert boot.dol, meta.xml, LICENSE.txt und THIRD_PARTY.txt nach apps/newlauncher/. Daten des Nutzers unter newlauncher/
(config.ini, emulators.ini, Favoriten, Verlauf, cache/ …) werden nie überschrieben, nur fehlende
Dateien aus dist/newlauncher/ angelegt. Der Launcher schreibt eine fehlende config.ini und
emulators.ini beim ersten Start selbst mit Standardwerten.

Selbsttest: nur auf eine eigene, leere Karte. Die Selbsttest-DOL fährt ein festes Skript über die
Fixture-Bibliothek und schreibt dabei Favoriten, Verlauf und Einstellungen.

Emulatoren: lädt die Pakete aus dem Open Shop Channel (Spec 6.9) und installiert sie mit denselben
Regeln wie der Launcher (osc_emulators.py). Ohne Namen alle aus dem Katalog; aktuelle werden
übersprungen. Ohne --yes erst Liste mit Größen zeigen und nachfragen.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from deploy_dolphin import ARGV_ECHO_DOL, DIST_DIR, FIXTURES_DIR, REPO_ROOT
from notices import REPO_ROOT as NOTICES_ROOT, write_notices
from osc_emulators import DEFAULT_REPO, EmuInstallError, deploy_emulators

RELEASE_DOL = REPO_ROOT / "build/wii/newlauncher.dol"
SELFTEST_DOL = REPO_ROOT / "build/wii/nl_selftest.dol"
WIILOAD_EXE = Path(r"C:\devkitPro\tools\bin\wiiload.exe")
LIBRARY_DIRS = ("wbfs", "games", "roms", "newlauncher")

SELFTEST_META = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>newlauncher Selbsttest</name>
  <coder>pudding-pudding00</coder>
  <short_description>Selbsttest mit Fixture-Bibliothek</short_description>
  <long_description>Nur auf einer eigenen, leeren SD-Karte starten.</long_description>
  <ahb_access/>
</app>
"""


class DeployError(Exception):
    pass


def set_ini_value(text: str, section: str, key: str, value: str | None) -> str:
    """Setzt `key = value` in [section] oder entfernt den Schlüssel (value=None). Kommentare, Reihenfolge
    und Zeilenenden bleiben; Verhalten wie core/ini (UTF-8-BOM ignoriert/bewahrt, case-sensitiv, alle
    gleichnamigen Abschnitte berücksichtigt)."""
    bom = "\ufeff" if text.startswith("\ufeff") else ""
    body = text[len(bom):]
    nl = "\r\n" if "\r\n" in body else ("\r" if "\r" in body and "\n" not in body else "\n")
    lines = body.splitlines(keepends=True)

    def header(line: str) -> str | None:
        s = line.strip()
        if s.startswith("[") and s.endswith("]") and "=" not in s:
            name = s[1:-1].strip()
            return name or None
        return None

    def key_of(line: str) -> str | None:
        s = line.strip()
        if not s or s[0] in ";#" or "=" not in s:
            return None
        name = s.split("=", 1)[0].strip()
        return name or None

    cur_section: str | None = None
    first_section_idx: int | None = None
    matches: list[int] = []
    for i, line in enumerate(lines):
        h = header(line)
        if h is not None:
            cur_section = h
            if h == section and first_section_idx is None:
                first_section_idx = i
            continue
        if cur_section == section and key_of(line) == key:
            matches.append(i)

    if value is None:
        if not matches:
            return text
        for i in reversed(matches):
            del lines[i]
        return bom + "".join(lines)

    if matches:
        first = matches[0]
        ending = lines[first][len(lines[first].rstrip("\r\n")):] or nl
        lines[first] = f"{key} = {value}{ending}"
        for i in reversed(matches[1:]):
            del lines[i]
        return bom + "".join(lines)

    if first_section_idx is None:
        out = body
        if out and not out.endswith(("\n", "\r")):
            out += nl
        if out:
            out += nl
        return bom + out + f"[{section}]{nl}{key} = {value}{nl}"

    end = next((i for i in range(first_section_idx + 1, len(lines)) if header(lines[i]) is not None), len(lines))
    last = first_section_idx
    for i in range(first_section_idx + 1, end):
        if lines[i].strip():
            last = i
    if not lines[last].endswith(("\n", "\r")):
        lines[last] += nl
    lines.insert(last + 1, f"{key} = {value}{nl}")
    return bom + "".join(lines)


def _need_bytes(paths) -> int:
    return sum(p.stat().st_size for p in paths if p.is_file())


def _check_space(sd: Path, need: int) -> None:
    free = shutil.disk_usage(sd).free
    if free < need + 64 * 1024:
        raise DeployError(f"Zu wenig Platz auf {sd}: {free // 1024} KiB frei, {need // 1024 + 64} KiB nötig")


def _copy_atomic(src: Path, dst: Path) -> None:
    """Kopiert über eine .tmp-Datei: bricht die Kopie ab (Karte voll), bleibt die alte Datei heil."""
    tmp = dst.with_name(dst.name + ".tmp")
    try:
        shutil.copy2(src, tmp)
        os.replace(tmp, dst)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


def _copy_missing(src: Path, dst: Path) -> None:
    """Kopiert Dateien aus src, die unter dst fehlen; vorhandene bleiben unberührt."""
    if not src.is_dir():
        return
    for path in src.rglob("*"):
        if path.is_file():
            target = dst / path.relative_to(src)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)


def deploy_release(sd: Path, dol: Path, init: bool) -> None:
    if not sd.is_dir():
        raise DeployError(f"Laufwerk nicht gefunden: {sd}")
    if not (sd / "apps").is_dir() and not init:
        raise DeployError(f"{sd} hat keinen apps/-Ordner (Homebrew-Karte?). Mit --init trotzdem anlegen.")
    if not dol.is_file():
        raise DeployError(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
    dist_files = [DIST_DIR / "apps/newlauncher" / n for n in ("meta.xml", "icon.png")]
    _check_space(sd, _need_bytes([dol, ARGV_ECHO_DOL, *dist_files, *(DIST_DIR / "newlauncher").rglob("*")])
                 + 256 * 1024)  # LICENSE.txt + THIRD_PARTY.txt
    app = sd / "apps/newlauncher"
    app.mkdir(parents=True, exist_ok=True)
    _copy_atomic(dol, app / "boot.dol")
    for src in dist_files:
        if src.is_file():
            _copy_atomic(src, app / src.name)
    write_notices(NOTICES_ROOT, app)  # Spec v2 3.4: Lizenz und Drittanbieter-Hinweise neben boot.dol
    _copy_missing(DIST_DIR / "newlauncher", sd / "newlauncher")
    if ARGV_ECHO_DOL.is_file():
        (sd / "apps/argv_echo").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ARGV_ECHO_DOL, sd / "apps/argv_echo/boot.dol")


def set_debug_flag(sd: Path, key: str, on: bool) -> None:
    """[debug] <key> = 1 in der config.ini der Karte setzen bzw. entfernen; der Rest der Datei bleibt."""
    cfg = sd / "newlauncher/config.ini"
    if not cfg.is_file() and not on:
        return
    raw = cfg.read_bytes() if cfg.is_file() else b""
    text = raw.decode("utf-8")
    out = set_ini_value(text, "debug", key, "1" if on else None)
    if out != text or (on and not cfg.is_file()):
        cfg.parent.mkdir(parents=True, exist_ok=True)
        cfg.write_bytes(out.encode("utf-8"))


def set_perf(sd: Path, on: bool) -> None:
    set_debug_flag(sd, "perf", on)


def deploy_selftest(sd: Path, dol: Path) -> None:
    if not sd.is_dir():
        raise DeployError(f"Laufwerk nicht gefunden: {sd}")
    found = [d for d in LIBRARY_DIRS if (sd / d).exists()]
    if found:
        raise DeployError(f"{sd} enthält schon {', '.join(found)}/. Die Selbsttest-DOL braucht eine eigene, "
                          "leere SD-Karte, weil sie Favoriten, Verlauf und Einstellungen schreibt.")
    if not dol.is_file():
        raise DeployError(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
    _check_space(sd, _need_bytes([dol, ARGV_ECHO_DOL, *FIXTURES_DIR.rglob("*"), *(DIST_DIR / "newlauncher").rglob("*")]))
    shutil.copytree(FIXTURES_DIR, sd, dirs_exist_ok=True)
    _copy_missing(DIST_DIR / "newlauncher", sd / "newlauncher")
    app = sd / "apps/nl_selftest"
    app.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dol, app / "boot.dol")
    (app / "meta.xml").write_text(SELFTEST_META, encoding="utf-8", newline="\n")
    if ARGV_ECHO_DOL.is_file():
        (sd / "apps/argv_echo").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ARGV_ECHO_DOL, sd / "apps/argv_echo/boot.dol")


def wiiload(dol: Path, ip: str) -> int:
    env = dict(os.environ, WIILOAD=f"tcp:{ip}")
    return subprocess.run([str(WIILOAD_EXE), str(dol)], env=env).returncode


def _drive(arg: str) -> Path:
    # „E:“ allein ist unter Windows das Arbeitsverzeichnis von E:, nicht die Wurzel.
    return Path(arg + "\\") if len(arg) == 2 and arg[1] == ":" else Path(arg)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("drive", nargs="?", help="Laufwerk oder Ordner der SD-Karte, z. B. E:")
    parser.add_argument("--init", action="store_true", help="auch ohne apps/-Ordner einrichten")
    parser.add_argument("--perf", choices=("on", "off"), help="[debug] perf in config.ini setzen bzw. entfernen")
    parser.add_argument("--remote", choices=("on", "off"),
                        help="[debug] remote in config.ini: Fernsteuerung auf TCP 4300 bei jedem Start (Fehlersuche)")
    parser.add_argument("--selftest", action="store_true", help="Selbsttest-Karte einrichten (leere Karte)")
    parser.add_argument("--wiiload", metavar="IP", help="DOL per wiiload an die Wii senden statt kopieren")
    parser.add_argument("--dol", type=Path, help="DOL (Standard: Release bzw. Selbsttest aus build/wii)")
    parser.add_argument("--emulators", nargs="*", metavar="NAME",
                        help="Emulatoren aus dem Open Shop Channel installieren/aktualisieren (ohne NAME: alle)")
    parser.add_argument("--repo", default=DEFAULT_REPO, help=f"Paketquelle für --emulators (Standard: {DEFAULT_REPO})")
    parser.add_argument("--yes", action="store_true", help="--emulators ohne Rückfrage")
    args = parser.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")  # Windows-Konsole (cp1252) kennt „→“ nicht
    dol = args.dol or (SELFTEST_DOL if args.selftest else RELEASE_DOL)
    try:
        if args.emulators is not None:
            if args.wiiload or args.selftest or args.perf or args.remote or args.init or args.dol:
                raise DeployError("--emulators nur allein mit Laufwerk, --repo und --yes")
            if not args.drive:
                raise DeployError("Laufwerk fehlt (z. B. E:)")
            sd = _drive(args.drive)
            if not (sd / "apps").is_dir():
                raise DeployError(f"{sd} hat keinen apps/-Ordner: ist das die Homebrew-Karte?")
            return deploy_emulators(sd, args.emulators, args.repo, args.yes)
        if args.wiiload:
            if args.drive or args.perf or args.remote or args.init:
                raise DeployError("--wiiload nicht zusammen mit Laufwerk, --perf, --remote oder --init verwenden")
            # Die Selbsttest-DOL schreibt Favoriten, Verlauf und Einstellungen auf die Karte in der Wii; per
            # wiiload lässt sich nicht prüfen, ob das eine leere Karte ist.
            if args.selftest or dol.name == SELFTEST_DOL.name:
                raise DeployError("Selbsttest nicht per wiiload: erst eine leere Karte mit --selftest einrichten")
            if not dol.is_file():
                raise DeployError(f"DOL nicht gefunden: {dol}")
            if not WIILOAD_EXE.is_file():
                raise DeployError(f"wiiload nicht gefunden: {WIILOAD_EXE}")
            return wiiload(dol, args.wiiload)
        if not args.drive:
            raise DeployError("Laufwerk fehlt (z. B. E:) oder --wiiload IP angeben")
        sd = _drive(args.drive)
        if args.selftest:
            if args.perf or args.remote:
                raise DeployError("--perf und --remote gelten nur für die Release-Karte")
            deploy_selftest(sd, dol)
            print(f"Selbsttest-Karte eingerichtet: {sd} (Homebrew Channel → „newlauncher Selbsttest“)")
            return 0
        deploy_release(sd, dol, args.init)
        if args.perf:
            set_perf(sd, args.perf == "on")
        if args.remote:
            set_debug_flag(sd, "remote", args.remote == "on")
        print(f"newlauncher nach {sd / 'apps/newlauncher'} kopiert" + (f", perf {args.perf}" if args.perf else "")
              + (f", remote {args.remote}" if args.remote else ""))
        return 0
    except (DeployError, EmuInstallError, OSError, UnicodeDecodeError) as e:
        print(f"DEPLOY FEHLER: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
