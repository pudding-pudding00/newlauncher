"""Minimaler PNG-Schreiber (RGBA8, Filter 0) für Fixture- und Test-Cover. Nur Standardbibliothek."""

import struct
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png_bytes(w: int, h: int, rgba: bytes) -> bytes:
    if len(rgba) != w * h * 4:
        raise ValueError("rgba passt nicht zu w*h*4")
    rows = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return PNG_SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(rows)) + _chunk(b"IEND", b"")


def solid_png(w: int, h: int, color: tuple[int, int, int, int]) -> bytes:
    return png_bytes(w, h, bytes(color) * (w * h))


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png_bytes(w, h, rgba))
