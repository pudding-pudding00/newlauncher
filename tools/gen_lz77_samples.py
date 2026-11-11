"""Erzeugt tests/host/support/lz77_samples.hpp: LZ77-Proben (Typ 0x10 und 0x11) als C++-Arrays.

Der Encoder ist bewusst einfach (gierig, Fenster 4096) und nur nach der Formatbeschreibung in
docs/superpowers/plans/2026-11-11-m11-lizenz-tls.md (Task 1, Step 1) geschrieben. Er dient allein dazu,
den C++-Dekoder in core/lz77 mit unabhängig erzeugten Daten zu prüfen.
Aufruf: python tools/gen_lz77_samples.py
"""

from __future__ import annotations

import random
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "tests/host/support/lz77_samples.hpp"

WINDOW = 4096
MIN_MATCH = 3
MAX_LEN = {0x10: 18, 0x11: 0x10110}
CANDIDATES = 64  # so viele frühere Positionen pro 3-Byte-Präfix werden geprüft


def _header(kind: int, size: int, extended: bool) -> bytes:
    if extended or size > 0xFFFFFF:
        return bytes([kind, 0, 0, 0]) + size.to_bytes(4, "little")
    return bytes([kind]) + size.to_bytes(3, "little")


def _ref(kind: int, length: int, dist: int) -> bytes:
    d = dist - 1
    if kind == 0x10:
        return bytes([((length - 3) << 4) | (d >> 8), d & 0xFF])
    if length <= 16:  # ind = length - 1 liegt in 2..15
        return bytes([((length - 1) << 4) | (d >> 8), d & 0xFF])
    if length <= 0x110:
        v = length - 0x11
        return bytes([v >> 4, ((v & 0xF) << 4) | (d >> 8), d & 0xFF])
    v = length - 0x111
    return bytes([0x10 | (v >> 12), (v >> 4) & 0xFF, ((v & 0xF) << 4) | (d >> 8), d & 0xFF])


def compress(data: bytes, kind: int, extended: bool = False, magic: bool = False) -> bytes:
    """Gieriger LZ77-Encoder; kind ist 0x10 oder 0x11."""
    assert kind in (0x10, 0x11)
    out = bytearray(b"LZ77" if magic else b"")
    out += _header(kind, len(data), extended)
    table: dict[bytes, list[int]] = {}
    tokens: list[bytes | int] = []  # int = Literal, bytes = Rückverweis
    pos = 0
    max_len = MAX_LEN[kind]
    while pos < len(data):
        best_len, best_dist = 0, 0
        if pos + MIN_MATCH <= len(data):
            for cand in reversed(table.get(data[pos:pos + MIN_MATCH], [])[-CANDIDATES:]):
                dist = pos - cand
                if dist > WINDOW:
                    break
                n = 0
                limit = min(max_len, len(data) - pos)
                while n < limit and data[cand + n] == data[pos + n]:
                    n += 1
                if n > best_len:
                    best_len, best_dist = n, dist
                    if n == limit:
                        break
        step = best_len if best_len >= MIN_MATCH else 1
        tokens.append(_ref(kind, best_len, best_dist) if best_len >= MIN_MATCH else data[pos])
        for p in range(pos, min(pos + step, len(data) - MIN_MATCH + 1)):
            table.setdefault(data[p:p + MIN_MATCH], []).append(p)
        pos += step
    for i in range(0, len(tokens), 8):
        group = tokens[i:i + 8]
        flags = 0
        body = bytearray()
        for bit, tok in enumerate(group):
            if isinstance(tok, int):
                body.append(tok)
            else:
                flags |= 0x80 >> bit
                body += tok
        out.append(flags)
        out += body
    return bytes(out)


def text_sample(size: int, seed: int = 7) -> bytes:
    words = ["Wii", "Kanal", "Banner", "Launcher", "Disc", "Spiel", "Menü", "Kachel", "Cover", "Liste"]
    rng = random.Random(seed)
    out = []
    while sum(len(w) + 1 for w in out) < size:
        out.append(rng.choice(words))
    return " ".join(out).encode()[:size]


def long_run_sample(size: int) -> bytes:
    rng = random.Random(99)
    buf = bytearray()
    while len(buf) < size:
        buf += bytes([rng.randrange(256)]) * rng.randrange(0x120, 0x1800)  # Läufe > 0x111
        buf += rng.randbytes(rng.randrange(5, 60))
        buf += text_sample(200, rng.randrange(1000))
    return bytes(buf[:size])


def samples() -> dict[str, tuple[bytes, bytes]]:
    """Name → (Klartext, gepackt)."""
    text = text_sample(5000)
    long = long_run_sample(70000)
    short = text_sample(300, 3)
    ext = text_sample(1200, 11)
    return {
        "kLz10Text": (text, compress(text, 0x10)),
        "kLz11Long": (long, compress(long, 0x11)),
        "kLz11Short": (short, compress(short, 0x11)),
        "kLz10Magic": (text, compress(text, 0x10, magic=True)),
        "kLz11Ext": (ext, compress(ext, 0x11, extended=True)),
    }


def _array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    body = ",\n    ".join(rows)
    return f"inline constexpr unsigned char {name}[] = {{\n    {body}\n}};\n"


def main() -> None:
    parts = ["#pragma once\n// Erzeugt von tools/gen_lz77_samples.py – nicht von Hand ändern.\n",
             "#include <cstddef>\n\nnamespace nl::test {\n\n"]
    for name, (plain, packed) in samples().items():
        parts.append(_array(f"{name}Data", packed)
                     + f"inline constexpr std::size_t {name}Size = {len(plain)};\n"
                     + f"inline constexpr unsigned {name}Crc = 0x{zlib.crc32(plain):08x}u;\n\n")
    parts.append("}  // namespace nl::test\n")
    OUT.write_text("".join(parts), encoding="utf-8", newline="\n")
    print(f"geschrieben: {OUT}")


if __name__ == "__main__":
    main()
