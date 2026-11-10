"""Schreibt und liest meta.bin (Spec 5.5): Big-Endian, Strings als u16-Länge + UTF-8, nach Schlüssel sortiert.

Wird von make_fixtures.py genutzt und später von prepare_meta.py (M5).
"""

import struct
from pathlib import Path

MAGIC = b"NLMD"
VERSION = 1


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


def write_meta_bin(path: Path, entries: dict[str, tuple[str, int, int, str]]) -> None:
    """entries: Schlüssel → (Titel, Jahr, Spieler, Beschreibung)."""
    out = bytearray(MAGIC + struct.pack(">HHI", VERSION, 0, len(entries)))
    for key in sorted(entries, key=lambda k: k.encode("utf-8")):
        title, year, players, desc = entries[key]
        out += _pack_str(key) + _pack_str(title) + struct.pack(">HB", year, players) + _pack_str(desc)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


def read_meta_bin(path: Path) -> dict[str, tuple[str, int, int, str]]:
    data = path.read_bytes()
    if data[:4] != MAGIC:
        raise ValueError("kein meta.bin")
    version, _, count = struct.unpack_from(">HHI", data, 4)
    if version != VERSION:
        raise ValueError("unbekannte Version")
    pos = 12

    def read_str() -> str:
        nonlocal pos
        (n,) = struct.unpack_from(">H", data, pos)
        s = data[pos + 2:pos + 2 + n].decode("utf-8")
        pos += 2 + n
        return s

    result: dict[str, tuple[str, int, int, str]] = {}
    for _ in range(count):
        key = read_str()
        title = read_str()
        year, players = struct.unpack_from(">HB", data, pos)
        pos += 3
        result[key] = (title, year, players, read_str())
    return result
