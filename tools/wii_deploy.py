"""Bringt den Launcher auf eine echte SD-Karte oder per wiiload auf die Wii (M7, Hardware-Abnahme).

    python tools/wii_deploy.py E: [--init] [--perf on|off] [--dol PATH]
    python tools/wii_deploy.py E: --selftest [--dol PATH]
    python tools/wii_deploy.py --wiiload 192.168.0.42 [--dol PATH]

Release: kopiert boot.dol und meta.xml nach apps/newlauncher/. Daten des Nutzers unter newlauncher/
(config.ini, emulators.ini, Favoriten, Verlauf, cache/ …) werden nie überschrieben, nur fehlende
Dateien aus dist/newlauncher/ angelegt. Der Launcher schreibt eine fehlende config.ini und
emulators.ini beim ersten Start selbst mit Standardwerten.

Selbsttest: nur auf eine eigene, leere Karte. Die Selbsttest-DOL fährt ein festes Skript über die
Fixture-Bibliothek und schreibt dabei Favoriten, Verlauf und Einstellungen.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from deploy_dolphin import ARGV_ECHO_DOL, DIST_DIR, FIXTURES_DIR, REPO_ROOT

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
    und Zeilenenden bleiben; ein fehlender Abschnitt kommt ans Ende."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)

    def header(line: str) -> str | None:
        s = line.strip()
        return s[1:-1].strip().lower() if s.startswith("[") and s.endswith("]") else None

    def key_of(line: str) -> str | None:
        s = line.strip()
        if not s or s[0] in ";#" or "=" not in s:
            return None
        return s.split("=", 1)[0].strip().lower()

    start = next((i for i, l in enumerate(lines) if header(l) == section.lower()), None)
    if start is None:
        if value is None:
            return text
        out = text
        if out and not out.endswith(("\n", "\r")):
            out += nl
        if out:
            out += nl
        return out + f"[{section}]{nl}{key} = {value}{nl}"

    end = next((i for i in range(start + 1, len(lines)) if header(lines[i]) is not None), len(lines))
    for i in range(start + 1, end):
        if key_of(lines[i]) == key.lower():
            if value is None:
                del lines[i]
            else:
                ending = lines[i][len(lines[i].rstrip("\r\n")):] or nl
                lines[i] = f"{key} = {value}{ending}"
            return "".join(lines)
    if value is None:
        return text
    last = start
    for i in range(start + 1, end):
        if lines[i].strip():
            last = i
    if not lines[last].endswith(("\n", "\r")):
        lines[last] += nl
    lines.insert(last + 1, f"{key} = {value}{nl}")
    return "".join(lines)


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
    app = sd / "apps/newlauncher"
    app.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dol, app / "boot.dol")
    for name in ("meta.xml", "icon.png"):
        if (DIST_DIR / "apps/newlauncher" / name).is_file():
            shutil.copy2(DIST_DIR / "apps/newlauncher" / name, app / name)
    _copy_missing(DIST_DIR / "newlauncher", sd / "newlauncher")
    if ARGV_ECHO_DOL.is_file():
        (sd / "apps/argv_echo").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ARGV_ECHO_DOL, sd / "apps/argv_echo/boot.dol")


def set_perf(sd: Path, on: bool) -> None:
    cfg = sd / "newlauncher/config.ini"
    raw = cfg.read_bytes() if cfg.is_file() else b""
    text = raw.decode("utf-8")
    out = set_ini_value(text, "debug", "perf", "1" if on else None)
    if out != text or not cfg.is_file():
        cfg.parent.mkdir(parents=True, exist_ok=True)
        cfg.write_bytes(out.encode("utf-8"))


def deploy_selftest(sd: Path, dol: Path) -> None:
    if not sd.is_dir():
        raise DeployError(f"Laufwerk nicht gefunden: {sd}")
    found = [d for d in LIBRARY_DIRS if (sd / d).exists()]
    if found:
        raise DeployError(f"{sd} enthält schon {', '.join(found)}/. Die Selbsttest-DOL braucht eine eigene, "
                          "leere SD-Karte, weil sie Favoriten, Verlauf und Einstellungen schreibt.")
    if not dol.is_file():
        raise DeployError(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
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
    parser.add_argument("--selftest", action="store_true", help="Selbsttest-Karte einrichten (leere Karte)")
    parser.add_argument("--wiiload", metavar="IP", help="DOL per wiiload an die Wii senden statt kopieren")
    parser.add_argument("--dol", type=Path, help="DOL (Standard: Release bzw. Selbsttest aus build/wii)")
    args = parser.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")  # Windows-Konsole (cp1252) kennt „→“ nicht
    dol = args.dol or (SELFTEST_DOL if args.selftest else RELEASE_DOL)
    try:
        if args.wiiload:
            if not dol.is_file():
                raise DeployError(f"DOL nicht gefunden: {dol}")
            if not WIILOAD_EXE.is_file():
                raise DeployError(f"wiiload nicht gefunden: {WIILOAD_EXE}")
            return wiiload(dol, args.wiiload)
        if not args.drive:
            raise DeployError("Laufwerk fehlt (z. B. E:) oder --wiiload IP angeben")
        sd = _drive(args.drive)
        if args.selftest:
            if args.perf:
                raise DeployError("--perf gilt nur für die Release-Karte")
            deploy_selftest(sd, dol)
            print(f"Selbsttest-Karte eingerichtet: {sd} (Homebrew Channel → „newlauncher Selbsttest“)")
            return 0
        deploy_release(sd, dol, args.init)
        if args.perf:
            set_perf(sd, args.perf == "on")
        print(f"newlauncher nach {sd / 'apps/newlauncher'} kopiert" + (f", perf {args.perf}" if args.perf else ""))
        return 0
    except (DeployError, OSError, UnicodeDecodeError) as e:
        print(f"DEPLOY FEHLER: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
