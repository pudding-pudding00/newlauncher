"""Prüft die Lage des Game-Booters (M9 Task 1): er läuft ab 0x80A80000 und darf den Launcher-Bereich nicht berühren.

Ohne Wii-Build (build/wii/game_booter.dol fehlt) wird der Test übersprungen, damit die Werkzeug-Tests überall laufen.
"""

import hashlib
import struct
import unittest
from pathlib import Path

DOL = Path(__file__).resolve().parents[2] / "build" / "wii" / "game_booter.dol"
HANDLER = DOL.parent / "codehandler" / "codehandler.bin"  # M15: im Build aus boot/nl/codehandler/codehandler.s
BASE = 0x80A80000
END = 0x81800000  # Ende MEM1 (24 MiB, gecacht)
MAX_SIZE = 10 * 1024 * 1024
CODEHANDLER_FINGERPRINTS = {
    "codehandler": "7c3243a67c3a02a67c7343a67c7b02a6",
    "codehandleronly": "4bffff487c892378409e04c83529ffff",
}


def dol_sections(data: bytes):
    """Liefert (Adresse, Größe) aller belegten Text- und Datensektionen, BSS-Bereich und Einsprung."""
    offs = struct.unpack_from(">18I", data, 0x00)
    addrs = struct.unpack_from(">18I", data, 0x48)
    sizes = struct.unpack_from(">18I", data, 0x90)
    sections = [(a, s) for o, a, s in zip(offs, addrs, sizes) if s]
    bss_addr, bss_size, entry = struct.unpack_from(">3I", data, 0xD8)
    return sections, (bss_addr, bss_size), entry


@unittest.skipUnless(DOL.exists(), "build/wii/game_booter.dol fehlt (kein Wii-Build)")
class BooterDolTest(unittest.TestCase):
    def setUp(self):
        self.data = DOL.read_bytes()
        self.sections, self.bss, self.entry = dol_sections(self.data)

    def test_entry(self):
        self.assertEqual(self.entry, BASE)

    def test_sections_in_range(self):
        self.assertTrue(self.sections)
        for addr, size in self.sections + [self.bss]:
            if size == 0:
                continue
            with self.subTest(addr=hex(addr), size=size):
                self.assertGreaterEqual(addr, BASE)
                self.assertLessEqual(addr + size, END)

    def test_size(self):
        self.assertLessEqual(len(self.data), MAX_SIZE)

    def test_no_codehandler_blob(self):
        # M11 Task 2: der Ocarina-Codehandler (Binärblob ohne Quelle) ist nicht im Booter.
        # Fingerabdrücke: je 16 Byte Maschinencode aus codehandler.h (0x2b4) und codehandleronly.h (0x244).
        for name, fp in CODEHANDLER_FINGERPRINTS.items():
            with self.subTest(blob=name):
                self.assertEqual(self.data.count(bytes.fromhex(fp)), 0)

    def test_codehandler_from_own_build(self):
        # M15: der Booter enthält genau den Handler, den der Build aus codehandler.s assembliert hat.
        handler = HANDLER.read_bytes()
        self.assertGreater(len(handler), 0x10C)
        self.assertEqual(handler[0x104:0x106], bytes.fromhex("3de0"))  # lis r15, codelist@h
        self.assertEqual(handler[0x108:0x10A], bytes.fromhex("61ef"))  # ori r15, r15, codelist@l
        at = self.data.find(handler)
        self.assertGreaterEqual(at, 0, "Codehandler aus dem Build fehlt im game_booter.dol")
        block = self.data[at:at + len(handler)]
        self.assertEqual(hashlib.sha256(block).hexdigest(), hashlib.sha256(handler).hexdigest())


class DolParserTest(unittest.TestCase):
    def test_parses_header(self):
        hdr = bytearray(0x100)
        struct.pack_into(">I", hdr, 0x00, 0x100)          # Text 0: Dateioffset
        struct.pack_into(">I", hdr, 0x48, BASE)           # Text 0: Adresse
        struct.pack_into(">I", hdr, 0x90, 0x20)           # Text 0: Größe
        struct.pack_into(">3I", hdr, 0xD8, BASE + 0x20, 0x40, BASE)
        sections, bss, entry = dol_sections(bytes(hdr))
        self.assertEqual(sections, [(BASE, 0x20)])
        self.assertEqual(bss, (BASE + 0x20, 0x40))
        self.assertEqual(entry, BASE)


if __name__ == "__main__":
    unittest.main()
