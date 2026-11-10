"""Richtet den Dolphin-Dev-Ordner ein und kopiert dist/ und die gebaute DOL in den SD-Sync-Ordner."""

import shutil
import sys
from pathlib import Path

from dolphin import DEFAULT_USER_DIR, REPO_ROOT, sd_sync_dir, write_dev_config

DEFAULT_DOL = REPO_ROOT / "build/wii/newlauncher.dol"
DIST_DIR = REPO_ROOT / "dist"
FIXTURES_DIR = REPO_ROOT / "tests/fixtures/sd"
ARGV_ECHO_DOL = REPO_ROOT / "build/wii/argv_echo.dol"


def deploy(dol: Path | None, sd_dir: Path, dist_dir: Path, fixtures_dir: Path | None = None,
           argv_echo: Path = ARGV_ECHO_DOL) -> None:
    """Kopiert dist/ (und optional die Fixture-Bibliothek) auf die SD und installiert `dol` als Launcher;
    dol=None lässt boot.dol unverändert. Die Test-DOL argv_echo kommt nach apps/argv_echo/, falls gebaut."""
    shutil.copytree(dist_dir, sd_dir, dirs_exist_ok=True)
    if fixtures_dir is not None:
        shutil.copytree(fixtures_dir, sd_dir, dirs_exist_ok=True)
    if argv_echo.is_file():
        (sd_dir / "apps/argv_echo").mkdir(parents=True, exist_ok=True)
        shutil.copy2(argv_echo, sd_dir / "apps/argv_echo/boot.dol")
    target = sd_dir / "apps/newlauncher"
    target.mkdir(parents=True, exist_ok=True)
    if dol is not None:
        shutil.copy2(dol, target / "boot.dol")


def main(user_dir: Path = DEFAULT_USER_DIR, dol: Path = DEFAULT_DOL, dist_dir: Path = DIST_DIR,
         fixtures_dir: Path | None = FIXTURES_DIR) -> int:
    if not dol.is_file():
        print(f"DEPLOY FEHLER: DOL nicht gefunden: {dol}")
        return 2
    write_dev_config(user_dir)
    sd = sd_sync_dir(user_dir)
    deploy(dol, sd, dist_dir, fixtures_dir)
    print(f"Deployt nach {sd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
