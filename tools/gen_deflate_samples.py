"""Erzeugt tests/host/support/deflate_samples.hpp: rohe Deflate-Ströme (ohne zlib-Kopf) als C++-Arrays.

Der Host-Compiler (WinLibs) hat kein zlib; die Proben entstehen deshalb einmal hier mit Pythons zlib und werden
eingecheckt. Aufruf: python tools/gen_deflate_samples.py
"""

from __future__ import annotations

import random
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "tests/host/support/deflate_samples.hpp"


def deflate(data: bytes, level: int, strategy: int = zlib.Z_DEFAULT_STRATEGY) -> bytes:
    c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return c.compress(data) + c.flush()


def text_sample(size: int) -> bytes:
    words = ["Wii", "Launcher", "Emulator", "Kachel", "Cover", "Spiel", "Konsole", "Zeiger", "Musik", "Liste"]
    rng = random.Random(1234)
    out = []
    while sum(len(w) + 1 for w in out) < size:
        out.append(rng.choice(words))
    return " ".join(out).encode()[:size]


def noisy_sample(size: int) -> bytes:
    rng = random.Random(42)
    buf = bytearray()
    while len(buf) < size:
        if buf and rng.random() < 0.5:
            start = rng.randrange(max(1, len(buf) - 30000), len(buf))
            n = rng.randrange(3, 258)
            for i in range(n):
                buf.append(buf[start + i % (len(buf) - start)])
        else:
            buf.extend(rng.randbytes(rng.randrange(1, 40)))
    return bytes(buf[:size])


def array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    body = ",\n    ".join(rows)
    return f"inline constexpr unsigned char {name}[] = {{\n    {body}\n}};\n"


def sample(name: str, plain: bytes, packed: bytes) -> str:
    return (array(f"{name}Data", packed)
            + f"inline constexpr std::size_t {name}Size = {len(plain)};\n"
            + f"inline constexpr unsigned {name}Crc = 0x{zlib.crc32(plain):08x}u;\n\n")


def main() -> None:
    stored = bytes(range(256)) + b"stored block!" * 4
    fixed = b"Hallo Wii, hallo Wii, hallo Emulator!"
    dynamic = text_sample(20000)
    multi = noisy_sample(200000)
    parts = [
        "#pragma once\n// Erzeugt von tools/gen_deflate_samples.py – nicht von Hand ändern.\n",
        "#include <cstddef>\n\nnamespace nl::test {\n\n",
        sample("kStored", stored, deflate(stored, 0)),
        sample("kFixed", fixed, deflate(fixed, 9, zlib.Z_FIXED)),
        sample("kDynamic", dynamic, deflate(dynamic, 9)),
        sample("kMultiBlock", multi, deflate(multi, 6)),
        "}  // namespace nl::test\n",
    ]
    OUT.write_text("".join(parts), encoding="utf-8", newline="\n")
    print(f"geschrieben: {OUT}")


if __name__ == "__main__":
    main()
