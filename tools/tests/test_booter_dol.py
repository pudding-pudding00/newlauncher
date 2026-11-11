"""Prüft die Lage des Game-Booters (M9 Task 1): er läuft ab 0x80A80000 und darf den Launcher-Bereich nicht berühren.

Ohne Wii-Build (build/wii/game_booter.dol fehlt) wird der Test übersprungen, damit die Werkzeug-Tests überall laufen.
"""

import struct
import unittest
from pathlib import Path

DOL = Path(__file__).resolve().parents[2] / "build" / "wii" / "game_booter.dol"
BASE = 0x80A80000
END = 0x81800000  # Ende MEM1 (24 MiB, gecacht)
MAX_SIZE = 10 * 1024 * 1024


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
