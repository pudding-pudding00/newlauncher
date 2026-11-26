"""Spielstand-Fixtures für die Dolphin-Smokes (M26).

Eine Fixture ist eine Test-Einheit mit selbst erzeugten Bytes (keine Spieldaten) an einem der drei Orte:
  gb      – GB-ROM der Fixtures mit save_dir = sd:/saves_smoke/gb (virtuelle SD)
  nand    – Super Mario Galaxy im Dolphin-NAND des Dev-Users (Wii/title/00010000/524d4750/data)
  emunand – dasselbe Spiel im Fixture-EmuNAND sd:/emunand mit [wii] save = emu

Schritte: backup (Bytes A sichern, ZIP aus dem SD-Abbild prüfen und aufheben), restore (Bytes B, die aufgehobene
Sicherung zurückspielen → wieder A), recover (liegengebliebenes Journal mit Phase „writing“ → Rollback auf A,
die hinzugekommene Datei verschwindet). Dolphin schreibt die virtuelle SD nicht in den Sync-Ordner zurück; was der
Launcher dort schreibt, liest der Smoke aus Load/WiiSD.raw.
"""

import hashlib
import io
import re
import shutil
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from dolphin import sd_sync_dir

FIXTURES = ("gb", "nand", "emunand")
STEPS = ("backup", "restore", "recover")

BYTES_A = b"NLSMOKE-A " * 50 + bytes(range(256))
BYTES_B = b"NLSMOKE-B " * 30
EXTRA_NAME = "nlextra.bin"  # NAND-Namen haben höchstens 12 Zeichen
META_NAME = "newlauncher-save.ini"
SAVES_REL = "newlauncher/saves"
JOURNAL_REL = SAVES_REL + "/restore.ini"
STATE_DIR = "save_smoke"  # im Dev-User: aufgehobene Sicherung je Fixture

_WII_DATA = "title/00010000/524d4750/data"  # RMGP → 0x524d4750
_GB_KEY = "gb/Tetris (World) (Rev 1).gb"
_GB_DIR = "saves_smoke/gb"


@dataclass
class SaveFixture:
    name: str
    key: str            # Spielschlüssel für save_backup/save_restore
    launcher_dir: str   # Ordner der Einheit, wie der Launcher ihn schreibt
    sd_rel: str | None  # derselbe Ordner relativ zur SD (None = Dolphin-NAND)
    host_dir: Path      # wo der Smoke vor dem Lauf schreibt
    file_name: str      # die eine Spielstand-Datei


@dataclass
class SavePlan:
    debug_lines: list[str] = field(default_factory=list)
    markers: list[str] = field(default_factory=list)
    config_append: str = ""
    emunand: bool = False


def fixture(name: str, user_dir: Path) -> SaveFixture:
    sd = sd_sync_dir(user_dir)
    if name == "gb":
        return SaveFixture(name, _GB_KEY, "sd:/" + _GB_DIR, _GB_DIR, sd / _GB_DIR, "Tetris (World) (Rev 1).sav")
    if name == "nand":
        return SaveFixture(name, "RMGP01", "nand:/" + _WII_DATA, None, user_dir / "Wii" / _WII_DATA, "smoke.sav")
    if name == "emunand":
        rel = "emunand/" + _WII_DATA
        return SaveFixture(name, "RMGP01", "sd:/" + rel, rel, sd / rel, "smoke.sav")
    raise ValueError(f"unbekannte Spielstand-Fixture: {name}")


def content_hash(files: dict[str, bytes]) -> str:
    """Inhalts-Hash wie core/save_archive: SHA-256 über sortierte Zeilen „pfad\\ngröße\\nsha256hex\\n“."""
    h = hashlib.sha256()
    for path in sorted(files):
        data = files[path]
        h.update(f"{path}\n{len(data)}\n{hashlib.sha256(data).hexdigest()}\n".encode())
    return h.hexdigest()


def journal_text(unit: str, launcher_dir: str, backup: str, added: list[str]) -> str:
    return (f"[restore]\nunit = {unit}\ndir = {launcher_dir}\nbackup = {backup}\nphase = writing\n"
            + "".join(f"added = {a}\n" for a in added))


def parse_backup_line(log: str) -> tuple[str, str] | None:
    m = re.search(r"NL_SAVE_BACKUP ok (\S+) (\S[^\r\n]*?)\s*$", log, re.MULTILINE)
    return (m.group(1), m.group(2)) if m else None


def verify_zip(data: bytes, unit: str, expected: dict[str, bytes]) -> list[str]:
    """Prüft ein Sicherungs-ZIP: Meta zuerst, Einheit, Hash neu gerechnet, Dateien wie erwartet."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            names = z.namelist()
            if not names or names[0] != META_NAME:
                return [f"ZIP: {META_NAME} ist nicht der erste Eintrag"]
            meta = dict(line.split(" = ", 1) for line in z.read(META_NAME).decode().splitlines() if " = " in line)
            files = {n: z.read(n) for n in names[1:]}
    except (zipfile.BadZipFile, UnicodeDecodeError, ValueError) as e:
        return [f"ZIP unlesbar: {e}"]
    problems = []
    if meta.get("unit") != unit:
        problems.append(f"ZIP: unit {meta.get('unit')!r} statt {unit!r}")
    if meta.get("hash") != content_hash(files):
        problems.append("ZIP: hash passt nicht zum Inhalt")
    if files != expected:
        problems.append(f"ZIP: Dateien {sorted(files)} entsprechen nicht {sorted(expected)}")
    return problems


def _state(user_dir: Path, fx: SaveFixture) -> tuple[Path, Path]:
    d = user_dir / STATE_DIR
    return d / f"{fx.name}.zip", d / f"{fx.name}.ini"


def _stored(user_dir: Path, fx: SaveFixture) -> tuple[bytes, str, str]:
    zip_path, ini_path = _state(user_dir, fx)
    if not zip_path.is_file() or not ini_path.is_file():
        raise FileNotFoundError(f"keine aufgehobene Sicherung für {fx.name}: erst --save-step backup")
    info = dict(line.split(" = ", 1) for line in ini_path.read_text(encoding="utf-8").splitlines() if " = " in line)
    return zip_path.read_bytes(), info["unit"], info["name"]


def _add_gb_save_dir(path: Path, value: str) -> None:
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")  # Checkout kann CRLF haben
    if "[gb]\n" not in text:
        raise ValueError(f"[gb] fehlt in {path}")
    path.write_text(text.replace("[gb]\n", f"[gb]\nsave_dir = {value}\n", 1), encoding="utf-8", newline="\n")


def prepare(fx: SaveFixture, step: str, user_dir: Path) -> SavePlan:
    """Legt die Einheit an und liefert Debug-Zeilen, Marker und Konfiguration für den Lauf."""
    sd = sd_sync_dir(user_dir)
    plan = SavePlan()
    if fx.name == "gb":
        _add_gb_save_dir(sd / "newlauncher/emulators.ini", fx.launcher_dir)
    if fx.name == "emunand":
        plan.emunand = True
        plan.config_append = "[wii]\nsave = emu\n"
    fx.host_dir.mkdir(parents=True, exist_ok=True)
    (fx.host_dir / EXTRA_NAME).unlink(missing_ok=True)
    if step == "backup":
        shutil.rmtree(sd / SAVES_REL, ignore_errors=True)
        (fx.host_dir / fx.file_name).write_bytes(BYTES_A)
        plan.debug_lines.append(f"save_backup = {fx.key}")
        plan.markers.append("NL_SAVE_BACKUP ok")
        return plan
    zip_bytes, unit, name = _stored(user_dir, fx)
    shutil.rmtree(sd / SAVES_REL, ignore_errors=True)
    unit_dir = sd / SAVES_REL / unit
    unit_dir.mkdir(parents=True)
    (unit_dir / name).write_bytes(zip_bytes)
    (fx.host_dir / fx.file_name).write_bytes(BYTES_B)
    if step == "restore":
        plan.debug_lines.append(f"save_restore = {fx.key}")
        plan.markers.append("NL_SAVE_RESTORE ok")
        return plan
    (fx.host_dir / EXTRA_NAME).write_bytes(BYTES_B)
    (sd / JOURNAL_REL).write_text(journal_text(unit, fx.launcher_dir, f"sd:/{SAVES_REL}/{unit}/{name}", [EXTRA_NAME]),
                                  encoding="utf-8", newline="\n")
    plan.markers.append("NL_SAVE_RECOVER rollback")
    return plan


def post_check(fx: SaveFixture, step: str, user_dir: Path, log: str, read_sd: Callable[[str], bytes]) -> list[str]:
    """Prüft nach dem Lauf; `read_sd(rel)` liest eine Datei aus dem SD-Abbild (FileNotFoundError, wenn sie fehlt)."""
    if step == "backup":
        found = parse_backup_line(log)
        if found is None:
            return ["NL_SAVE_BACKUP ok <einheit> <pfad>"]
        unit, path = found
        if not path.startswith("sd:/"):
            return [f"Sicherung nicht auf der SD: {path}"]
        try:
            data = read_sd(path[len("sd:/"):])
        except (OSError, ValueError) as e:
            return [f"Sicherung nicht im SD-Abbild: {path} ({e})"]
        problems = verify_zip(data, unit, {fx.file_name: BYTES_A})
        if not problems:
            zip_path, ini_path = _state(user_dir, fx)
            zip_path.parent.mkdir(parents=True, exist_ok=True)
            zip_path.write_bytes(data)
            ini_path.write_text(f"unit = {unit}\nname = {path.rsplit('/', 1)[1]}\n", encoding="utf-8")
        return problems

    def read(name: str) -> bytes | None:
        try:
            if fx.sd_rel is None:
                p = fx.host_dir / name
                return p.read_bytes() if p.is_file() else None
            return read_sd(f"{fx.sd_rel}/{name}")
        except (OSError, KeyError):
            return None

    problems = []
    if read(fx.file_name) != BYTES_A:
        problems.append(f"{fx.launcher_dir}/{fx.file_name} hat nicht die gesicherten Bytes")
    if step == "recover" and read(EXTRA_NAME) is not None:
        problems.append(f"{fx.launcher_dir}/{EXTRA_NAME} wurde nicht entfernt")
    return problems


def cleanup(fx: SaveFixture, user_dir: Path) -> None:
    """Räumt die Einheit und die Sicherungen auf der SD weg; die aufgehobene Sicherung bleibt."""
    sd = sd_sync_dir(user_dir)
    for name in (fx.file_name, EXTRA_NAME):
        (fx.host_dir / name).unlink(missing_ok=True)
    if fx.name == "gb":
        shutil.rmtree(sd / "saves_smoke", ignore_errors=True)
    shutil.rmtree(sd / SAVES_REL, ignore_errors=True)
