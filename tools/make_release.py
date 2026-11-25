"""Baut das Release-ZIP (Spec v2 3.4).

    python tools/make_release.py --out dist/newlauncher-<version>.zip [--dol build/wii/newlauncher.dol]

Inhalt: apps/newlauncher/{boot.dol, meta.xml, icon.png, LICENSE.txt, THIRD_PARTY.txt}. meta.xml und
icon.png kommen aus dist/apps/newlauncher/, soweit vorhanden. Der Quelltext steht im Git-Tag des Releases.

Daneben entsteht update.ini (M20, Selbst-Update): Version, ZIP-Name und -Größe, entpackte Größe. Beide Dateien
kommen als Assets ins GitHub-Release; newlauncher liest .../releases/latest/download/update.ini.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
import zipfile
from pathlib import Path

from notices import REPO_ROOT, write_notices

APP = "apps/newlauncher"


def cmake_version(root: Path) -> str | None:
    try:
        text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    m = re.search(r"project\(\s*newlauncher\s+VERSION\s+([0-9.]+)", text)
    return m.group(1) if m else None


def write_update_ini(zip_path: Path, version: str) -> Path:
    with zipfile.ZipFile(zip_path) as z:
        unzipped = sum(i.file_size for i in z.infolist())
    ini = zip_path.parent / "update.ini"
    lines = ["[update]", f"version = {version}", f"zip = {zip_path.name}",
             f"zip_size = {zip_path.stat().st_size}", f"unzipped_size = {unzipped}", ""]
    ini.write_bytes("\n".join(lines).encode("utf-8"))
    return ini


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Release-ZIP bauen")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--dol", type=Path, default=None)
    ap.add_argument("--root", type=Path, default=REPO_ROOT)
    ap.add_argument("--version", default=None, help="Standard: VERSION aus CMakeLists.txt")
    args = ap.parse_args(argv)
    version = args.version or cmake_version(args.root) or "0.0.0"
    dol = args.dol or args.root / "build/wii/newlauncher.dol"
    if not dol.is_file():
        print(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
        return 1
    dist_app = args.root / "dist" / APP
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dol, f"{APP}/boot.dol")
        for name in ("meta.xml", "icon.png"):
            if (dist_app / name).is_file():
                z.write(dist_app / name, f"{APP}/{name}")
        for path in write_notices(args.root, Path(tmp)):
            z.write(path, f"{APP}/{path.name}")
    ini = write_update_ini(args.out, version)
    print(f"Release: {args.out} + {ini.name} (Version {version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
