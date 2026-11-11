"""Baut das Release-ZIP (Spec v2 3.4).

    python tools/make_release.py --out dist/newlauncher-<version>.zip [--dol build/wii/newlauncher.dol]

Inhalt: apps/newlauncher/{boot.dol, meta.xml, icon.png, LICENSE.txt, THIRD_PARTY.txt}. meta.xml und
icon.png kommen aus dist/apps/newlauncher/, soweit vorhanden. Der Quelltext steht im Git-Tag des Releases.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
import zipfile
from pathlib import Path

from notices import REPO_ROOT, write_notices

APP = "apps/newlauncher"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Release-ZIP bauen")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--dol", type=Path, default=None)
    ap.add_argument("--root", type=Path, default=REPO_ROOT)
    args = ap.parse_args(argv)
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
    print(f"Release: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
