"""Lädt Retro-Boxarts von libretro-thumbnails nach <lib>/newlauncher/covers_src/<konsole>/<stamm>.png (Spec 6.4).

Wii/GC-Cover lädt der Launcher selbst über „Fehlende Cover laden“ (GameTDB). Die Umwandlung nach .tex passiert
erst auf der Wii. Vorhandene PNGs bleiben, außer mit --force.

Aufruf: python tools/prepare_covers.py <lib> [--force]
"""

import argparse
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pngfile import PNG_SIGNATURE  # noqa: E402
from prepare_meta import RETRO  # noqa: E402

THUMB_BASE = "https://thumbnails.libretro.com"
_RESERVED = set('&*/:`<>?\\|"')


def thumbnail_name(stem: str) -> str:
    """libretro ersetzt &*/:`<>?\\|" im Dateinamen durch _."""
    return "".join("_" if c in _RESERVED else c for c in stem)


def thumbnail_url(console: str, stem: str) -> str:
    system = RETRO[console][2]
    name = urllib.parse.quote(thumbnail_name(stem) + ".png", safe="")
    return f"{THUMB_BASE}/{urllib.parse.quote(system, safe='')}/Named_Boxarts/{name}"


def _default_fetch(url: str) -> bytes | None:
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            return r.read()
    except (urllib.error.URLError, TimeoutError, OSError):
        return None


def main(argv: list[str] | None = None, fetch: Callable[[str], "bytes | None"] = _default_fetch) -> dict[str, int]:
    ap = argparse.ArgumentParser(description="Lädt Retro-Boxarts von libretro-thumbnails.")
    ap.add_argument("lib", type=Path, help="Wurzel des Bibliotheksgeräts (SD oder USB)")
    ap.add_argument("--force", action="store_true", help="vorhandene PNGs neu laden")
    args = ap.parse_args(argv)

    counts = {"ok": 0, "vorhanden": 0, "fehlt": 0}
    for console, (folder, exts, _) in RETRO.items():
        root = args.lib / folder
        if not root.is_dir():
            continue
        for rom in sorted(root.iterdir()):
            if not rom.is_file() or rom.suffix.lower() not in exts:
                continue
            stem = rom.name.rsplit(".", 1)[0]
            target = args.lib / "newlauncher" / "covers_src" / console / f"{stem}.png"
            if target.is_file() and not args.force:
                counts["vorhanden"] += 1
                continue
            data = fetch(thumbnail_url(console, stem))
            if not data or not data.startswith(PNG_SIGNATURE):
                counts["fehlt"] += 1
                print(f"fehlt: {console}/{stem}")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            tmp = target.with_suffix(".tmp")
            tmp.write_bytes(data)
            tmp.replace(target)
            counts["ok"] += 1
    print(f"Cover: {counts['ok']} geladen, {counts['vorhanden']} vorhanden, {counts['fehlt']} nicht gefunden")
    return counts


if __name__ == "__main__":
    main()
    sys.exit(0)
