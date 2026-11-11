"""Prüft den Encoder in tools/gen_lz77_samples.py mit einem eigenen Python-Dekoder (M11 Task 1).

Der Dekoder hier ist unabhängig vom C++-Code geschrieben, nur nach der Formatbeschreibung im M11-Plan.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import gen_lz77_samples as gen  # noqa: E402


def decode(data: bytes) -> bytes:
    pos = 4 if data[:4] == b"LZ77" else 0
    kind = data[pos]
    assert kind in (0x10, 0x11)
    size = int.from_bytes(data[pos + 1:pos + 4], "little")
    pos += 4
    if size == 0:
        size = int.from_bytes(data[pos:pos + 4], "little")
        pos += 4
    out = bytearray()
    while len(out) < size:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(data[pos])
                pos += 1
                continue
            b0 = data[pos]
            if kind == 0x10:
                b1 = data[pos + 1]
                length, dist = (b0 >> 4) + 3, ((b0 & 0xF) << 8 | b1) + 1
                pos += 2
            else:
                ind = b0 >> 4
                if ind == 0:
                    b1, b2 = data[pos + 1], data[pos + 2]
                    length = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11
                    dist = ((b1 & 0xF) << 8 | b2) + 1
                    pos += 3
                elif ind == 1:
                    b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
                    length = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111
                    dist = ((b2 & 0xF) << 8 | b3) + 1
                    pos += 4
                else:
                    b1 = data[pos + 1]
                    length, dist = ind + 1, ((b0 & 0xF) << 8 | b1) + 1
                    pos += 2
            assert dist <= len(out), "Rückverweis vor den Anfang"
            for _ in range(min(length, size - len(out))):
                out.append(out[-dist])
    return bytes(out)


class GenLz77SamplesTest(unittest.TestCase):
    def test_every_sample_round_trips(self):
        for name, (plain, packed) in gen.samples().items():
            with self.subTest(name=name):
                self.assertEqual(decode(packed), plain)
                self.assertLess(len(packed), len(plain), "Probe soll tatsächlich komprimiert sein")

    def test_headers(self):
        s = gen.samples()
        self.assertEqual(s["kLz10Text"][1][0], 0x10)
        self.assertEqual(s["kLz11Long"][1][0], 0x11)
        self.assertEqual(s["kLz10Magic"][1][:5], b"LZ77\x10")
        self.assertEqual(s["kLz11Ext"][1][:4], b"\x11\x00\x00\x00")

    def test_long_sample_uses_four_byte_references(self):
        packed = gen.compress(b"\x42" * 0x400, 0x11)
        # Kopf (4), Flag-Byte, Literal 0x42, dann ein Rückverweis mit ind == 1
        self.assertEqual(packed[6] >> 4, 1)
        self.assertEqual(decode(packed), b"\x42" * 0x400)

    def test_short_lengths_for_both_kinds(self):
        for kind in (0x10, 0x11):
            for data in (b"a", b"abcabc", b"abcabcabcabcabcabcabcabcabcabcabc"):
                with self.subTest(kind=kind, data=data):
                    self.assertEqual(decode(gen.compress(data, kind)), data)


if __name__ == "__main__":
    unittest.main()
