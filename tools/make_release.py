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


META_VERSION_RE = re.compile(r"(<version>)[^<]*(</version>)")


def meta_with_version(meta_xml: str, version: str) -> str:
    """meta.xml with its <version> element set to the release version; unchanged when there is none."""
    return META_VERSION_RE.sub(lambda m: f"{m.group(1)}{version}{m.group(2)}", meta_xml, count=1)


def write_update_ini(zip_path: Path, version: str, zip_base_url: str | None = None) -> Path:
    with zipfile.ZipFile(zip_path) as z:
        unzipped = sum(i.file_size for i in z.infolist())
    ini = zip_path.parent / "update.ini"
    zip_ref = f"{zip_base_url.rstrip('/')}/{zip_path.name}" if zip_base_url else zip_path.name
    lines = ["[update]", f"version = {version}", f"zip = {zip_ref}",
             f"zip_size = {zip_path.stat().st_size}", f"unzipped_size = {unzipped}", ""]
    ini.write_bytes("\n".join(lines).encode("utf-8"))
    return ini


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Release-ZIP bauen")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--dol", type=Path, default=None)
    ap.add_argument("--root", type=Path, default=REPO_ROOT)
    ap.add_argument("--version", default=None, help="Standard: VERSION aus CMakeLists.txt")
    ap.add_argument("--zip-base-url", default=None,
                    help="https directory the launcher downloads the ZIP from (default: next to update.ini)")
    args = ap.parse_args(argv)
    if args.zip_base_url and not args.zip_base_url.startswith("https://"):
        print("--zip-base-url must start with https:// (the launcher downloads over HTTPS only)")
        return 1
    version = args.version or cmake_version(args.root) or "0.0.0"
    dol = args.dol or args.root / "build/wii/newlauncher.dol"
    if not dol.is_file():
        print(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
        return 1
    dist_app = args.root / "dist" / APP
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dol, f"{APP}/boot.dol")
        meta = dist_app / "meta.xml"
        if meta.is_file():
            z.writestr(f"{APP}/meta.xml", meta_with_version(meta.read_text(encoding="utf-8"), version))
        if (dist_app / "icon.png").is_file():
            z.write(dist_app / "icon.png", f"{APP}/icon.png")
        for path in write_notices(args.root, Path(tmp)):
            z.write(path, f"{APP}/{path.name}")
    ini = write_update_ini(args.out, version, args.zip_base_url)
    print(f"Release: {args.out} + {ini.name} (Version {version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
