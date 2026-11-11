"""Prüft, dass jede gelinkte Bibliothek und jeder Ordner unter third_party/ in THIRD_PARTY.md steht (Spec v2 3.4).

    python tools/check_licenses.py build/wii [--root REPO]

Liest die Linkzeilen der Release-Ziele (CMakeFiles/<ziel>.dir/link.txt). Exit 1 mit Liste der
fehlenden Einträge, sonst 0.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
LINK_TARGETS = ("newlauncher", "nl_game_booter")
# Teil der Toolchain (newlib/libgcc/libstdc++), keine Drittkomponente des Releases.
SYSTEM_LIBS = {"m", "c", "gcc", "stdc++", "sysbase", "supc++", "g"}
# Ordner unter third_party/, die nur Tests dienen und nicht verteilt werden.
TEST_ONLY_DIRS = {"doctest"}
# -l<name> → Teilstring, der in der Name-Spalte von THIRD_PARTY.md vorkommen muss (klein geschrieben).
ALIASES = {
    "png16": "libpng",
    "png": "libpng",
    "z": "zlib",
    "bz2": "bzip2",
    "brotlidec": "brotli",
    "brotlicommon": "brotli",
    "ogg": "libogg",
    "vorbisidec": "tremor",
    "fat": "libfat",
    "ogc": "libogc",
    "asnd": "libogc",
    "bte": "libogc",
    "wiiuse": "libogc",
    "wiitls": "wii_tls",
    "mbedtls": "mbed tls",
    "mbedx509": "mbed tls",
    "mbedcrypto": "mbed tls",
    "tfpsacrypto": "mbed tls",
}


def table_names(markdown: str) -> list[str]:
    """Erste Spalte jeder Tabellenzeile, klein geschrieben, ohne Kopf- und Trennzeile."""
    names = []
    for line in markdown.splitlines():
        if not line.lstrip().startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 5:
            continue
        name = cells[0].lower()
        if name in ("name", "") or set(name) <= set("-: "):
            continue
        names.append(name)
    return names


def component_for_lib(lib: str) -> str:
    return ALIASES.get(lib, lib).lower()


def is_listed(key: str, names: list[str]) -> bool:
    return any(key in n for n in names)


def linked_libs(build_dir: Path) -> tuple[set[str], list[Path]]:
    libs: set[str] = set()
    found = []
    for target in LINK_TARGETS:
        link = build_dir / "CMakeFiles" / f"{target}.dir" / "link.txt"
        if link.is_file():
            found.append(link)
            libs.update(re.findall(r"(?:^|\s)-l([A-Za-z0-9_+.-]+)", link.read_text(encoding="utf-8")))
    return libs - SYSTEM_LIBS, found


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Lizenzeinträge für gelinkte Bibliotheken prüfen")
    ap.add_argument("build_dir", type=Path)
    ap.add_argument("--root", type=Path, default=REPO_ROOT)
    args = ap.parse_args(argv)

    names = table_names((args.root / "THIRD_PARTY.md").read_text(encoding="utf-8"))
    libs, links = linked_libs(args.build_dir)
    if not links:
        print(f"Keine link.txt der Ziele {', '.join(LINK_TARGETS)} unter {args.build_dir} – erst bauen.")
        return 1
    missing = [f"-l{lib}" for lib in sorted(libs) if not is_listed(component_for_lib(lib), names)]
    tp = args.root / "third_party"
    if tp.is_dir():
        for d in sorted(p.name for p in tp.iterdir() if p.is_dir()):
            if d not in TEST_ONLY_DIRS and not is_listed(d.lower(), names):
                missing.append(f"third_party/{d}")
    if missing:
        print("Ohne Eintrag in THIRD_PARTY.md:")
        for m in missing:
            print(f"  {m}")
        return 1
    print(f"Lizenzen ok: {len(libs)} Bibliotheken, {len(names)} Einträge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
