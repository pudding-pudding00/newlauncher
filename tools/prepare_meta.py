"""Erzeugt <lib>/newlauncher/cache/meta.bin (Spec 6.3) auf dem PC.

Wii/GC: Titel, Jahr, Spieler und Beschreibung aus GameTDBs wiitdb.xml (https://www.gametdb.com/Wii/Downloads,
entpackt; Pfad über --wiitdb). Retro: Jahr und Spieler aus den libretro-Datenbanken (metadat/releaseyear und
metadat/maxusers), Titel aus dem No-Intro-Dateinamen ohne Tags. Die DAT-Dateien werden nach --dat-dir geladen
und dort zwischengespeichert; --offline nutzt nur, was schon da ist.

Aufruf: python tools/prepare_meta.py <lib> [--wiitdb wiitdb.xml] [--dat-dir DIR] [--offline] [--lang DE]
"""

import argparse
import re
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from metabin import meta_key, write_meta_bin  # noqa: E402

DAT_BASE = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat"
DAT_FIELDS = {"releaseyear": "releaseyear", "maxusers": "users"}  # Ordner → Feldname im DAT

# Konsole → (Ordner, Endungen, libretro-Systemname); Reihenfolge wie core/console.cpp.
RETRO = {
    "nes": ("roms/nes", (".nes",), "Nintendo - Nintendo Entertainment System"),
    "snes": ("roms/snes", (".sfc", ".smc"), "Nintendo - Super Nintendo Entertainment System"),
    "gb": ("roms/gb", (".gb",), "Nintendo - Game Boy"),
    "n64": ("roms/n64", (".z64", ".n64", ".v64"), "Nintendo - Nintendo 64"),
    "gbc": ("roms/gbc", (".gbc",), "Nintendo - Game Boy Color"),
    "gba": ("roms/gba", (".gba",), "Nintendo - Game Boy Advance"),
}
DISCS = {"gc": ("games", (".iso",)), "wii": ("wbfs", (".wbfs", ".iso"))}

WII_MAGIC, GC_MAGIC = 0x5D1C9EA3, 0xC2339F3D

Fetch = Callable[[str], "bytes | None"]


def _disc_id(path: Path, console: str) -> str | None:
    with path.open("rb") as f:
        head = f.read(1024)
    if head[:4] == b"WBFS" and len(head) >= 9:
        at = 1 << head[8]
        with path.open("rb") as f:
            f.seek(at)
            head = f.read(256)
    if len(head) < 0x20:
        return None
    magic_at = 0x18 if console == "wii" else 0x1C
    magic = WII_MAGIC if console == "wii" else GC_MAGIC
    if struct.unpack_from(">I", head, magic_at)[0] != magic:
        return None
    id6 = head[:6].decode("ascii", "replace")
    return id6 if re.fullmatch(r"[A-Z0-9]{6}", id6) else None


def scan_library(lib: Path) -> list[tuple[str, str, Path]]:
    """(Spiel-Schlüssel, Konsole, Datei) wie der Scanner auf der Wii (Spec 5.2/5.3)."""
    games: list[tuple[str, str, Path]] = []
    for console, (folder, exts, _) in RETRO.items():
        root = lib / folder
        if root.is_dir():
            for f in sorted(root.iterdir()):
                if f.is_file() and f.suffix.lower() in exts:
                    games.append((f"{console}/{f.name}", console, f))
    for console, (folder, exts) in DISCS.items():
        root = lib / folder
        if not root.is_dir():
            continue
        for d in sorted(p for p in root.iterdir() if p.is_dir()):
            for f in sorted(d.iterdir()):
                if f.is_file() and f.suffix.lower() in exts:
                    id6 = _disc_id(f, console)
                    if id6:
                        games.append((id6, console, f))
                    break
    return games


def parse_wiitdb(path: Path, ids: set[str], lang: str) -> dict[str, tuple[str, int, int, str]]:
    """ID6 → (Titel, Jahr, Spieler, Beschreibung); Sprache lang, sonst EN, sonst die erste."""
    out: dict[str, tuple[str, int, int, str]] = {}
    for _, elem in ET.iterparse(path, events=("end",)):
        if elem.tag != "game":
            continue
        gid = (elem.findtext("id") or "").strip()
        if gid in ids:
            locales = {loc.get("lang", ""): loc for loc in elem.findall("locale")}
            loc = next((locales[k] for k in (lang, "EN") if k in locales), next(iter(locales.values()), None))
            title = (loc.findtext("title") or "").strip() if loc is not None else ""
            desc = (loc.findtext("synopsis") or "").strip() if loc is not None else ""
            date = elem.find("date")
            year = _int(date.get("year") if date is not None else None)
            inp = elem.find("input")
            players = _int(inp.get("players") if inp is not None else None)
            out[gid] = (title or gid, year, min(players, 255), desc)
        elem.clear()
    return out


def _int(s: str | None) -> int:
    try:
        return max(0, int(s or 0))
    except ValueError:
        return 0


_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\()|(\))|([^\s()"]+)')


def parse_dat(text: str, field: str) -> dict[str, tuple[str, str]]:
    """clrmamepro-DAT: No-Intro-Name (comment, sonst name) → (Wert von field, CRC32 groß)."""
    out: dict[str, tuple[str, str]] = {}
    tokens = [m.group(1) if m.group(1) is not None else m.group(0) for m in _TOKEN.finditer(text)]
    i = 0
    while i < len(tokens):
        if tokens[i] == "game" and i + 1 < len(tokens) and tokens[i + 1] == "(":
            i += 2
            depth, props, crc = 1, {}, ""
            while i < len(tokens) and depth > 0:
                t = tokens[i]
                if t == "(":
                    depth += 1
                elif t == ")":
                    depth -= 1
                elif i + 1 < len(tokens) and tokens[i + 1] not in ("(", ")"):
                    if depth == 1:
                        props.setdefault(t, tokens[i + 1])
                    elif t == "crc":
                        crc = tokens[i + 1].upper()
                    i += 1
                i += 1
            name = props.get("comment") or props.get("name")
            if name and field in props:
                out[name] = (props[field], crc)
        else:
            i += 1
    return out


def retro_title(stem: str) -> str:
    """No-Intro-Name ohne (Region), (Sprachen), [!] usw."""
    title = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", stem).strip()
    return title or stem


def _crc(path: Path) -> str:
    crc = 0
    with path.open("rb") as f:
        while chunk := f.read(1 << 20):
            crc = zlib.crc32(chunk, crc)
    return f"{crc:08X}"


def _lookup(table: dict[str, tuple[str, str]], name: str, crc_index: dict[str, str], crc: Callable[[], str]) -> int:
    if name in table:
        return _int(table[name][0])
    return _int(crc_index.get(crc()))


def build_entries(games: list[tuple[str, str, Path]], wii_meta: dict[str, tuple[str, int, int, str]],
                  dats: dict[str, dict[str, dict[str, tuple[str, str]]]]) -> dict[str, tuple[str, int, int, str]]:
    entries: dict[str, tuple[str, int, int, str]] = {}
    for key, console, path in games:
        if console in DISCS:
            if key in wii_meta:
                entries[key] = wii_meta[key]
            continue
        stem = path.name.rsplit(".", 1)[0]
        tables = dats.get(console, {})
        years, users = tables.get("releaseyear", {}), tables.get("users", {})
        name_hit = stem in years or stem in users
        crc_value: list[str] = []

        def crc() -> str:
            if not crc_value:
                crc_value.append(_crc(path))
            return crc_value[0]

        name = stem
        if not name_hit:  # umbenannte Datei: No-Intro-Namen über die CRC32 finden
            by_crc = {c: n for n, (_, c) in {**users, **years}.items() if c}
            name = by_crc.get(crc(), stem)
        title = retro_title(name)
        year = _lookup(years, stem, {c: v for v, c in years.values() if c}, crc)
        players = _lookup(users, stem, {c: v for v, c in users.values() if c}, crc)
        entries[meta_key(key)] = (title, min(year, 0xFFFF), min(players, 255), "")
    return entries


def _default_fetch(url: str) -> bytes | None:
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            return r.read()
    except (urllib.error.URLError, TimeoutError, OSError):
        return None


def fetch_dats(dat_dir: Path, fetch: Fetch = _default_fetch) -> None:
    """Lädt fehlende DATs nach dat_dir/<releaseyear|maxusers>/<System>.dat; vorhandene bleiben."""
    for folder in DAT_FIELDS:
        for _, _, system in RETRO.values():
            target = dat_dir / folder / f"{system}.dat"
            if target.is_file():
                continue
            data = fetch(f"{DAT_BASE}/{folder}/{urllib.parse.quote(system)}.dat")
            if data:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)


def load_dats(dat_dir: Path) -> dict[str, dict[str, dict[str, tuple[str, str]]]]:
    dats: dict[str, dict[str, dict[str, tuple[str, str]]]] = {}
    for console, (_, _, system) in RETRO.items():
        for folder, field in DAT_FIELDS.items():
            path = dat_dir / folder / f"{system}.dat"
            if path.is_file():
                text = path.read_text(encoding="utf-8", errors="replace")
                dats.setdefault(console, {})[field] = parse_dat(text, field)
    return dats


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Erzeugt newlauncher/cache/meta.bin aus GameTDB und libretro.")
    ap.add_argument("lib", type=Path, help="Wurzel des Bibliotheksgeräts (SD oder USB)")
    ap.add_argument("--wiitdb", type=Path, help="entpackte wiitdb.xml von gametdb.com")
    ap.add_argument("--dat-dir", type=Path, default=Path(__file__).resolve().parents[1] / ".cache" / "libretro")
    ap.add_argument("--offline", action="store_true", help="keine DATs herunterladen")
    ap.add_argument("--lang", default="DE", help="Sprache der Wii/GC-Titel (Rückfall EN)")
    args = ap.parse_args(argv)

    games = scan_library(args.lib)
    disc_ids = {key for key, console, _ in games if console in DISCS}
    wii_meta = parse_wiitdb(args.wiitdb, disc_ids, args.lang.upper()) if args.wiitdb else {}
    if not args.offline:
        fetch_dats(args.dat_dir)
    entries = build_entries(games, wii_meta, load_dats(args.dat_dir))
    write_meta_bin(args.lib / "newlauncher" / "cache" / "meta.bin", entries)
    discs = sum(1 for k in entries if k in disc_ids)
    print(f"Einträge: {len(entries)} (Wii/GC {discs}, Retro {len(entries) - discs})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
