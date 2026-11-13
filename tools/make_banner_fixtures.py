"""Banner-Fixtures für Tests und Smokes (M18). Alles aus eigenen Daten, keine fremden Banner-Dateien.

Formatbeschreibungen stehen an den Parsern in src/core (gc_banner, gc_fst, später u8_archive, tpl, brlyt, …).

    python tools/make_banner_fixtures.py <ausgabeordner>
"""

import hashlib
import struct
import sys
from pathlib import Path

BANNER_W, BANNER_H = 96, 32


def rgb5a3_pixel(r: int, g: int, b: int) -> int:
    """Opakes RGB555-Pixel (Bit 15 gesetzt) aus 8-Bit-Kanälen."""
    return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)


def tile_rgb5a3(pixels: list[int], w: int, h: int) -> bytes:
    """Zeilenweise Pixel → GX-Kacheln 4×4, 2 Byte big endian je Pixel."""
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(4):
                for x in range(4):
                    out += struct.pack(">H", pixels[(ty + y) * w + tx + x])
    return bytes(out)


def banner_image() -> bytes:
    """Waagerechter Verlauf blau → türkis mit hellem Rahmen, damit man die Lage auf dem Bildschirm erkennt."""
    px = []
    for y in range(BANNER_H):
        for x in range(BANNER_W):
            if x in (0, BANNER_W - 1) or y in (0, BANNER_H - 1):
                px.append(rgb5a3_pixel(240, 240, 240))
            else:
                px.append(rgb5a3_pixel(20, 60 + x * 150 // BANNER_W, 200))
    return tile_rgb5a3(px, BANNER_W, BANNER_H)


def gc_bnr(blocks: list[tuple[str, str, str, str, str]], bnr2: bool = True) -> bytes:
    """BNR1 (ein Block) oder BNR2 (sechs Blöcke EN, DE, FR, ES, IT, NL); Texte Latin-1."""
    count = 6 if bnr2 else 1
    data = bytearray(0x1820 + count * 0x140)
    data[0:4] = b"BNR2" if bnr2 else b"BNR1"
    data[0x20:0x1820] = banner_image()
    for i in range(count):
        st, sm, t, m, d = blocks[i] if i < len(blocks) else blocks[0]
        base = 0x1820 + i * 0x140
        for off, text, cap in ((0, st, 0x20), (0x20, sm, 0x20), (0x40, t, 0x40), (0x80, m, 0x40), (0xC0, d, 0x80)):
            raw = text.encode("latin-1")[:cap]
            data[base + off:base + off + len(raw)] = raw
    return bytes(data)


def fst(files: list[tuple[str, bytes]], data_start: int) -> tuple[bytes, list[int]]:
    """FST nur mit Dateien in der Wurzel; liefert FST und die Offsets der Dateien (32-Byte-ausgerichtet)."""
    names = bytearray()
    entries = [(0x01000000, 0, len(files) + 1)]
    offsets = []
    at = data_start
    for name, payload in files:
        entries.append((len(names), at, len(payload)))
        offsets.append(at)
        names += name.encode("ascii") + b"\0"
        at = (at + len(payload) + 31) & ~31
    table = b"".join(struct.pack(">III", *e) for e in entries)
    return table + bytes(names), offsets


def gc_iso(header: bytes, files: list[tuple[str, bytes]]) -> bytes:
    """GC-Abbild: Disc-Kopf (mindestens 0x440 Byte), FST bei 0x2000, Dateien dahinter."""
    fst_at = 0x2000
    probe, _ = fst(files, 0)
    data_start = (fst_at + len(probe) + 31) & ~31
    table, offsets = fst(files, data_start)
    end = offsets[-1] + len(files[-1][1]) if files else data_start
    image = bytearray(max(end, fst_at + len(table)))
    image[:len(header)] = header
    struct.pack_into(">II", image, 0x424, fst_at, len(table))
    image[fst_at:fst_at + len(table)] = table
    for (_, payload), off in zip(files, offsets):
        image[off:off + len(payload)] = payload
    return bytes(image)


def u8_archive(tree: dict) -> bytes:
    """U8-Archiv aus einem verschachtelten dict (Name -> bytes oder dict); Daten 32-Byte-ausgerichtet."""
    nodes = [[0x01000000, 0, 0]]
    names = bytearray(b"\0")
    files = []

    def add(d: dict, parent: int) -> None:
        for name, value in d.items():
            off = len(names)
            names.extend(name.encode("ascii") + b"\0")
            idx = len(nodes)
            if isinstance(value, dict):
                nodes.append([0x01000000 | off, parent, 0])
                add(value, idx)
                nodes[idx][2] = len(nodes)
            else:
                nodes.append([off, 0, len(value)])
                files.append((idx, value))

    add(tree, 0)
    nodes[0][2] = len(nodes)
    header = len(nodes) * 12 + len(names)
    at = (0x20 + header + 31) & ~31
    data_start = at
    for idx, payload in files:
        nodes[idx][1] = at
        at = (at + len(payload) + 31) & ~31
    out = bytearray(at)
    struct.pack_into(">IIII", out, 0, 0x55AA382D, 0x20, header, data_start)
    for i, n in enumerate(nodes):
        struct.pack_into(">III", out, 0x20 + i * 12, *n)
    out[0x20 + len(nodes) * 12:0x20 + header] = names
    for idx, payload in files:
        out[nodes[idx][1]:nodes[idx][1] + len(payload)] = payload
    return bytes(out)


def imd5(payload: bytes) -> bytes:
    """IMD5-Hülle: Magic, Größe, 8 Byte frei, MD5 der Nutzdaten, dann die Nutzdaten."""
    return b"IMD5" + struct.pack(">I", len(payload)) + bytes(8) + hashlib.md5(payload).digest() + payload


def sample_bnr() -> bytes:
    en = ("Fixture", "NL", "Fixture Banner", "newlauncher", "Test banner made by make_banner_fixtures.py")
    de = ("Fixture", "NL", "Fixture-Banner", "newlauncher", "Testbanner aus make_banner_fixtures.py")
    return gc_bnr([en, de, en, en, en, en])


def main(out: Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    (out / "opening.bnr").write_bytes(sample_bnr())
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(Path(sys.argv[1])))
