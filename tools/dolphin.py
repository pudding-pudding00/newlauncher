"""Dolphin-Dev-Umgebung: eigener User-Ordner, SD-Sync, Logausgabe und Marker-Suche."""

import time
from pathlib import Path
from typing import Callable

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DOLPHIN = REPO_ROOT.parent / "wii_tools/Dolphin-x64/Dolphin.exe"
DEFAULT_USER_DIR = REPO_ROOT.parent / "wii_tools/dolphin-user"


def sd_sync_dir(user_dir: Path) -> Path:
    return user_dir / "SDSync"


def log_path(user_dir: Path) -> Path:
    return user_dir / "Logs" / "dolphin.log"


def write_dev_config(user_dir: Path) -> None:
    """Schreibt (überschreibt) Dolphin.ini und Logger.ini des Dev-User-Ordners."""
    config = user_dir / "Config"
    config.mkdir(parents=True, exist_ok=True)
    (config / "Dolphin.ini").write_text(
        "[Core]\n"
        "WiiSDCard = True\n"
        "WiiSDCardEnableFolderSync = True\n"
        "WiiSDCardAllowWrites = True\n"
        "[General]\n"
        f"WiiSDCardSyncFolder = {sd_sync_dir(user_dir)}\n"
        "[Interface]\n"
        "ConfirmStop = False\n"
        "UsePanicHandlers = False\n"
        "[Analytics]\n"
        "Enabled = False\n"
        "PermissionAsked = True\n"
        "[AutoUpdate]\n"
        "UpdateTrack = \n",
        encoding="utf-8",
    )
    (config / "Logger.ini").write_text(
        "[Options]\n"
        "WriteToFile = True\n"
        "WriteToConsole = False\n"
        "WriteToWindow = False\n"
        "Verbosity = 1\n"
        "[Logs]\n"
        "OSREPORT = True\n"
        "OSREPORT_HLE = True\n",
        encoding="utf-8",
    )


def dolphin_command(exe: Path, user_dir: Path, dol: Path) -> list[str]:
    """Startbefehl; -u isoliert die Konfiguration vom privaten Dolphin des Nutzers."""
    return [str(exe), "-u", str(user_dir), "-b", "-e", str(dol)]


def reset_log(user_dir: Path) -> None:
    """Entfernt das Log eines früheren Laufs, damit ein alter Marker nicht zählt."""
    log_path(user_dir).unlink(missing_ok=True)


def _read_log(log: Path) -> str:
    """Logtext oder "" – ein fehlendes oder gerade gesperrtes Log zählt als leer."""
    try:
        return log.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def wait_for_markers(log: Path, markers: list[str], timeout_s: float,
                     is_alive: Callable[[], bool], poll_s: float = 0.25) -> list[str]:
    """Wartet, bis alle `markers` im Log stehen; liefert die fehlenden (leer = Erfolg).

    Endet der Prozess, wird das Log noch einmal gelesen: Er kann den letzten Marker
    zwischen zwei Abfragen geschrieben haben.
    """
    deadline = time.monotonic() + timeout_s
    while True:
        text = _read_log(log)
        missing = [m for m in markers if m not in text]
        if not missing:
            return []
        if not is_alive():
            text = _read_log(log)
            return [m for m in markers if m not in text]
        if time.monotonic() >= deadline:
            return missing
        time.sleep(poll_s)
