#!/bin/bash
# Builds a devkitPro dkp-pacman package (.pkg.tar.zst) from packaging/dkp-wii/PKGBUILD.
# Run inside a devkitPro MSYS2 / Linux environment with makepkg installed.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKGDIR="$ROOT/packaging/dkp-wii"
OUT="$ROOT/dist/pkg"

command -v makepkg >/dev/null 2>&1 || {
    echo "makepkg not found (install pacman/makepkg or run in devkitPro MSYS2)" >&2
    exit 1
}

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$PKGDIR/PKGBUILD" "$OUT/PKGBUILD"

(
    cd "$OUT"
    WTLS_SRC_ROOT="$ROOT" PKGDEST="$ROOT/dist" makepkg -f --nodeps --noconfirm
)
echo "Built package in $ROOT/dist/"
