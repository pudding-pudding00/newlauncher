"""Kopiert dist/ und die gebaute DOL in den SD-Sync-Ordner von Dolphin."""

import shutil
import sys
from pathlib import Path

from dolphin import DEFAULT_USER_DIR, REPO_ROOT, sd_sync_dir

DEFAULT_DOL = REPO_ROOT / "build/wii/newlauncher.dol"
DIST_DIR = REPO_ROOT / "dist"


def deploy(dol: Path, sd_dir: Path, dist_dir: Path) -> None:
    shutil.copytree(dist_dir, sd_dir, dirs_exist_ok=True)
    target = sd_dir / "apps/newlauncher"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dol, target / "boot.dol")


def main() -> int:
    if not DEFAULT_DOL.is_file():
        print(f"DEPLOY FEHLER: DOL nicht gefunden: {DEFAULT_DOL}")
        return 2
    sd = sd_sync_dir(DEFAULT_USER_DIR)
    deploy(DEFAULT_DOL, sd, DIST_DIR)
    print(f"Deployt nach {sd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
