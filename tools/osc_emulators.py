"""Emulatoren aus dem Open Shop Channel am PC auf die SD-Karte bringen (Spec 6.9, Gegenstück zu
src/core/emu_catalog.cpp und emu_install.cpp).

Gleiche Regeln wie im Launcher: Liste listv036.txt, ZIP-Größe muss stimmen, sichere Pfade, CRC beim
Entpacken; Dateien unter apps/ werden ersetzt, alle anderen nur angelegt, wenn sie fehlen; boot.dol
zuletzt; Stand in newlauncher/emulators_installed.ini im selben Format.
"""

from __future__ import annotations

import os
import shutil
import tempfile
import zipfile
from pathlib import Path
from typing import Callable, NamedTuple
from urllib.request import urlopen

DEFAULT_REPO = "http://hbb1.oscwii.org/hbb"
INSTALLED_INI = "newlauncher/emulators_installed.ini"
STAGE_DIR = "newlauncher/tmp"
_INI_HEADER = "; von newlauncher geschrieben (Spec 6.9)\n"
_BLOCK_LINES = 7
MAX_LIST_BYTES = 1024 * 1024  # wie kMaxOscListBytes in src/platform/emu_worker.hpp
HTTP_TIMEOUT = 30


class EmuInfo(NamedTuple):
    name: str
    label: str


# Reihenfolge und Namen wie emulatorCatalog() in src/core/emu_catalog.cpp (Test vergleicht).
CATALOG = (EmuInfo("fceugx", "FCE Ultra GX (NES)"),
           EmuInfo("snes9xgx", "Snes9x GX (SNES)"),
           EmuInfo("vbagx", "Visual Boy Advance GX (GB/GBC/GBA)"),
           EmuInfo("not64", "Not64 (N64)"),
           EmuInfo("wii64", "Wii64 (N64)"),
           EmuInfo("Nintendont", "Nintendont (GameCube)"))


class OscApp(NamedTuple):
    name: str
    timestamp: int
    unzipped_size: int
    zip_size: int
    display_name: str
    version: str


class EmuInstallError(Exception):
    pass


def _u32(s: str) -> int | None:
    if not s or len(s) > 10 or not s.isdigit() or not s.isascii():
        return None
    v = int(s)
    return v if v <= 0xFFFFFFFF else None


def parse_osc_list(text: str) -> dict[str, OscApp]:
    """Blöcke zu je sieben Zeilen; Kopfzeile, =Kategorien=, kaputte und abgeschnittene Blöcke fallen weg."""
    lines = [line[:-1] if line.endswith("\r") else line for line in text.split("\n")]
    apps: dict[str, OscApp] = {}
    i = 0
    while i < len(lines):
        line = lines[i]
        f = line.split()
        nums = [_u32(f[k]) for k in (1, 2, 3, 5)] if len(f) >= 6 else [None]
        if not line or line.startswith("=") or line.startswith("Homebrew ") or None in nums:
            i += 1
            continue
        if i + _BLOCK_LINES > len(lines):
            break
        apps[f[0]] = OscApp(f[0], nums[0], nums[2], nums[3], lines[i + 1].strip(), lines[i + 3].strip())
        i += _BLOCK_LINES
    return apps


def list_url(repo: str) -> str:
    return repo.rstrip("/") + "/listv036.txt"


def zip_url(repo: str, name: str) -> str:
    return f"{repo.rstrip('/')}/{name}/{name}.zip"


def parse_installed_emus(text: str) -> dict[str, tuple[int, str]]:
    """Nur Katalog-Namen mit gültigem timestamp, wie parseInstalledEmus."""
    sections: dict[str, dict[str, str]] = {}
    current = None
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            current = sections.setdefault(line[1:-1].strip(), {})
        elif current is not None and "=" in line:
            key, _, value = line.partition("=")
            current[key.strip()] = value.strip()
    out = {}
    for info in CATALOG:
        values = sections.get(info.name, {})
        ts = _u32(values.get("timestamp", ""))
        if ts is not None:
            out[info.name] = (ts, values.get("version", ""))
    return out


def format_installed_emus(installed: dict[str, tuple[int, str]]) -> str:
    out = _INI_HEADER
    for name in sorted(installed, key=lambda n: n.encode("utf-8")):  # std::map-Reihenfolge
        ts, version = installed[name]
        out += f"[{name}]\ntimestamp = {ts}\nversion = {version}\n"
    return out


def is_safe_zip_path(name: str) -> bool:
    if not name or name.startswith("/") or any(c in "\\:" or ord(c) < 0x20 for c in name):
        return False
    return all(seg not in ("", ".", "..") for seg in name.rstrip("/").split("/"))


def _replace(staged: Path, dest: Path) -> None:
    """dest → dest.old, staged → dest; schlägt der zweite Schritt fehl, kommt .old zurück."""
    if not dest.exists():
        os.replace(staged, dest)
        return
    old = dest.with_name(dest.name + ".old")
    old.unlink(missing_ok=True)
    os.replace(dest, old)
    try:
        os.replace(staged, dest)
    except OSError:
        os.replace(old, dest)
        raise
    old.unlink(missing_ok=True)


def install_emulator(sd: Path, zip_path: Path, app: OscApp) -> int:
    """Installiert das geladene ZIP; gibt die Anzahl Dateien zurück. Bei EmuInstallError bleibt die Karte,
    wie sie war (die Zwischenablage newlauncher/tmp wird geleert)."""
    if zip_path.stat().st_size != app.zip_size:
        raise EmuInstallError(f"{app.name}: ZIP-Größe {zip_path.stat().st_size} statt {app.zip_size}")
    stage = sd / STAGE_DIR / app.name
    shutil.rmtree(stage, ignore_errors=True)
    try:
        try:
            with zipfile.ZipFile(zip_path) as z:
                infos = [i for i in z.infolist() if not i.is_dir()]
                for info in z.infolist():
                    # orig_filename: zipfile ersetzt unter Windows "\" und streicht Laufwerke; die Wii sieht
                    # den Rohnamen, also prüfen wir den.
                    if not is_safe_zip_path(info.orig_filename) or info.orig_filename != info.filename:
                        raise EmuInstallError(f"{app.name}: unsicherer Pfad im ZIP: {info.orig_filename}")
                for info in infos:
                    target = stage / info.filename
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(z.read(info))  # prüft die CRC
        except (zipfile.BadZipFile, zipfile.LargeZipFile, EOFError) as e:
            raise EmuInstallError(f"{app.name}: Paket beschädigt: {e}") from e

        names = sorted((i.filename for i in infos), key=lambda n: n.rsplit("/", 1)[-1] == "boot.dol")
        for name in names:
            dest = sd / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            if name.startswith("apps/"):
                _replace(stage / name, dest)
            elif not dest.exists():
                os.replace(stage / name, dest)

        ini = sd / INSTALLED_INI
        try:
            installed = parse_installed_emus(ini.read_text(encoding="utf-8")) if ini.is_file() else {}
        except UnicodeDecodeError:
            installed = {}
        installed[app.name] = (app.timestamp, app.version)
        ini.parent.mkdir(parents=True, exist_ok=True)
        tmp = ini.with_name(ini.name + ".tmp")
        tmp.write_text(format_installed_emus(installed), encoding="utf-8", newline="\n")
        os.replace(tmp, ini)
        return len(names)
    finally:
        shutil.rmtree(sd / STAGE_DIR, ignore_errors=True)


def _fetch(url: str, limit: int) -> bytes:
    """Lädt höchstens limit Bytes; mehr ist ein Fehler (wie httpGet mit Obergrenze)."""
    with urlopen(url, timeout=HTTP_TIMEOUT) as r:
        data = r.read(limit + 1)
    if len(data) > limit:
        raise EmuInstallError(f"{url}: Antwort größer als {limit} Bytes")
    return data


def deploy_emulators(sd: Path, names: list[str], repo: str, yes: bool,
                     ask: Callable[[str], str] | None = None) -> int:
    """CLI-Ablauf von wii_deploy --emulators. 0 = fertig, 1 = vom Nutzer abgebrochen.
    Fehler (unbekannter Name, Netz, Paket) als EmuInstallError bzw. OSError."""
    known = [info.name for info in CATALOG]
    unknown = [n for n in names if n not in known]
    if unknown:
        raise EmuInstallError(f"unbekannte Emulatoren: {', '.join(unknown)} (bekannt: {', '.join(known)})")
    wanted = names or known
    apps = parse_osc_list(_fetch(list_url(repo), MAX_LIST_BYTES).decode("utf-8", "replace"))
    ini = sd / INSTALLED_INI
    installed = parse_installed_emus(ini.read_text(encoding="utf-8", errors="replace")) if ini.is_file() else {}

    todo: list[OscApp] = []
    for name in wanted:
        app = apps.get(name)
        if app is None:
            print(f"  {name}: nicht in der Liste von {repo}")
        elif name in installed and installed[name][0] >= app.timestamp:
            print(f"  {name}: aktuell ({installed[name][1] or app.version})")
        else:
            todo.append(app)
    if not todo:
        print("Nichts zu installieren.")
        return 0
    for app in todo:
        old = installed.get(app.name)
        state = f"{old[1] or '?'} → {app.version}" if old else app.version
        print(f"  {app.name}: {state}, {app.zip_size / 1e6:.1f} MB")
    if not yes and (ask or input)(f"{len(todo)} Paket(e) laden und nach {sd} installieren? [j/N] ").strip().lower() != "j":
        print("Abgebrochen, nichts geladen.")
        return 1
    with tempfile.TemporaryDirectory(prefix="nl_emu_") as tmp:
        for app in todo:
            path = Path(tmp) / f"{app.name}.zip"
            path.write_bytes(_fetch(zip_url(repo, app.name), app.zip_size))
            n = install_emulator(sd, path, app)
            print(f"  {app.name} {app.version}: {n} Dateien installiert")
    return 0
