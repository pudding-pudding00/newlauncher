"""Schreibt und liest meta.bin (Spec 5.5): Big-Endian, Strings als u16-Länge + UTF-8, nach Schlüssel sortiert.

Wird von make_fixtures.py genutzt und später von prepare_meta.py (M5).
"""

import struct
from pathlib import Path

MAGIC = b"NLMD"
VERSION = 2  # M17: je Eintrag zusätzlich u32 Genres, u8 Bewertungssystem, u8 Alter


def meta_key(game_key: str) -> str:
    """Metadaten-Schlüssel (Spec 5.2): ID6 unverändert, ROMs ohne Endung, nur ASCII kleingeschrieben."""
    if "/" not in game_key:
        return game_key
    stem = game_key.rsplit(".", 1)[0] if "." in game_key.split("/", 1)[1] else game_key
    return "".join(c.lower() if "A" <= c <= "Z" else c for c in stem)


def _pack_str(s: str) -> bytes:
    data = s.encode("utf-8")
    if len(data) > 0xFFFF:
        raise ValueError("String zu lang für meta.bin")
    return struct.pack(">H", len(data)) + data


def write_meta_bin(path: Path, entries: dict[str, tuple]) -> None:
    """entries: Schlüssel → (Titel, Jahr, Spieler, Beschreibung[, Genres, Bewertungssystem, Alter]).

    Ohne die letzten drei Werte: keine Genres, unbewertet.
    """
    out = bytearray(MAGIC + struct.pack(">HHI", VERSION, 0, len(entries)))
    for key in sorted(entries, key=lambda k: k.encode("utf-8")):
        title, year, players, desc, genres, system, age = (tuple(entries[key]) + (0, 0, 0))[:7]
        out += _pack_str(key) + _pack_str(title) + struct.pack(">HB", year, players) + _pack_str(desc)
        out += struct.pack(">IBB", genres, system, age)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


def read_meta_bin(path: Path) -> dict[str, tuple[str, int, int, str, int, int, int]]:
    """Liest Version 1 und 2; liefert immer 7-Tupel (v1: Genres 0, unbewertet)."""
    data = path.read_bytes()
    if data[:4] != MAGIC:
        raise ValueError("kein meta.bin")
    version, _, count = struct.unpack_from(">HHI", data, 4)
    if version not in (1, 2):
        raise ValueError("unbekannte Version")
    pos = 12

    def read_str() -> str:
        nonlocal pos
        (n,) = struct.unpack_from(">H", data, pos)
        s = data[pos + 2:pos + 2 + n].decode("utf-8")
        pos += 2 + n
        return s

    result: dict[str, tuple[str, int, int, str, int, int, int]] = {}
    for _ in range(count):
        key = read_str()
        title = read_str()
        year, players = struct.unpack_from(">HB", data, pos)
        pos += 3
        desc = read_str()
        genres = system = age = 0
        if version >= 2:
            genres, system, age = struct.unpack_from(">IBB", data, pos)
            pos += 6
        result[key] = (title, year, players, desc, genres, system, age)
    return result
