"""Tests für fat_frag.py: erwartete NL_FRAG-Zeile aus einem FAT32-Image (M9 Task 5).

Die Fragmente und die CRC hier sind dieselben wie im Host-Test „frag_list: crc wie fat_frag.py“.
"""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from fat_frag import NotFat32Error, expected_frag_line, read_file  # noqa: E402

PART_LBA = 2048
SPC = 1
RESERVED = 32
TOTAL = 70000  # ≈ 68 900 Cluster → FAT32


class SparseImage:
    """Bild als dict Sektor → bytearray, beim Schreiben eine dünn besetzte Datei."""

    def __init__(self):
        self.sectors: dict[int, bytearray] = {}

    def sector(self, lba: int) -> bytearray:
        return self.sectors.setdefault(lba, bytearray(512))

    def write(self, lba: int, off: int, data: bytes) -> None:
        while data:
            s = self.sector(lba + off // 512)
            o = off % 512
            n = min(len(data), 512 - o)
            s[o:o + n] = data[:n]
            data = data[n:]
            off += n

    def save(self, path: Path, total_sectors: int) -> None:
        with path.open("wb") as f:
            f.truncate(total_sectors * 512)
            for lba, s in sorted(self.sectors.items()):
                f.seek(lba * 512)
                f.write(s)


def lfn_checksum(short: bytes) -> int:
    s = 0
    for c in short:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def dir_entries(name: str, short: bytes, attr: int, cluster: int, size: int) -> bytes:
    """LFN-Einträge (rückwärts) plus 8.3-Eintrag."""
    out = b""
    if name.upper() != short.decode().replace(" ", "").replace("ISO", ".ISO") or "[" in name:
        units = [ord(c) for c in name] + [0]
        units += [0xFFFF] * (-len(units) % 13)
        chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
        chk = lfn_checksum(short)
        for seq in range(len(chunks), 0, -1):
            u = chunks[seq - 1]
            e = bytearray(32)
            e[0] = seq | (0x40 if seq == len(chunks) else 0)
            for i, c in enumerate(u[:5]):
                struct.pack_into("<H", e, 1 + 2 * i, c)
            e[11] = 0x0F
            e[13] = chk
            for i, c in enumerate(u[5:11]):
                struct.pack_into("<H", e, 14 + 2 * i, c)
            for i, c in enumerate(u[11:13]):
                struct.pack_into("<H", e, 28 + 2 * i, c)
            out += bytes(e)
    e = bytearray(32)
    e[0:11] = short
    e[11] = attr
    struct.pack_into("<H", e, 20, cluster >> 16)
    struct.pack_into("<H", e, 26, cluster & 0xFFFF)
    struct.pack_into("<I", e, 28, size)
    return out + bytes(e)


class Fat32:
    def __init__(self, img: SparseImage, fat16: bool = False):
        self.img = img
        entry = 2 if fat16 else 4
        self.fat_sz = ((TOTAL // SPC + 2) * entry + 511) // 512
        root_dir_sectors = 32 if fat16 else 0
        self.fat_lba = PART_LBA + RESERVED
        self.data_lba = self.fat_lba + 2 * self.fat_sz + root_dir_sectors
        total = 30000 if fat16 else TOTAL
        b = img.sector(PART_LBA)
        b[0:3] = b"\xEB\x58\x90"
        b[3:11] = b"MSWIN4.1"
        struct.pack_into("<HBHB", b, 0x0B, 512, SPC, RESERVED, 2)
        b[0x15] = 0xF8
        if fat16:
            struct.pack_into("<H", b, 0x11, 512)
            struct.pack_into("<H", b, 0x16, self.fat_sz)
        else:
            struct.pack_into("<I", b, 0x24, self.fat_sz)
            struct.pack_into("<I", b, 0x2C, 2)
        struct.pack_into("<I", b, 0x20, total)
        b[510:512] = b"\x55\xAA"
        mbr = img.sector(0)
        mbr[0x1BE + 4] = 0x06 if fat16 else 0x0C
        struct.pack_into("<II", mbr, 0x1BE + 8, PART_LBA, total)
        mbr[510:512] = b"\x55\xAA"

    def lba(self, cluster: int) -> int:
        return self.data_lba + (cluster - 2) * SPC

    def chain(self, clusters: list[int]) -> None:
        for i, c in enumerate(clusters):
            nxt = clusters[i + 1] if i + 1 < len(clusters) else 0x0FFFFFFF
            self.img.write(self.fat_lba, c * 4, struct.pack("<I", nxt))


def build_image(path: Path, fat16: bool = False) -> None:
    img = SparseImage()
    fs = Fat32(img, fat16)
    if not fat16:
        fs.chain([2])  # Stammverzeichnis
        fs.chain([3])  # Spielordner
        fs.chain([10, 11, 20])  # ISO in zwei Fragmenten
        img.write(fs.lba(2), 0, dir_entries("Super Mario Galaxy [RMGP01]", b"SUPERM~1   ", 0x10, 3, 0))
        img.write(fs.lba(3), 0, dir_entries("RMGP01.iso", b"RMGP01  ISO", 0x20, 10, 3 * 512 - 100))
        for i, c in enumerate([10, 11, 20]):
            img.write(fs.lba(c), 0, bytes([0x41 + i]) * 512)
    img.save(path, PART_LBA + TOTAL)


class FatFragTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "WiiSD.raw"

    def tearDown(self):
        self.tmp.cleanup()

    def test_expected_line_for_two_fragments(self):
        build_image(self.path)
        # Cluster 10/11 → LBA 3182, 3183; Cluster 20 → LBA 3192 (data_lba = 2048 + 32 + 2 × 547)
        line = expected_frag_line(self.path, "Super Mario Galaxy [RMGP01]/RMGP01.iso")
        self.assertEqual(line, "NL_FRAG dev=sd part_lba=2048 n=2 size=3 crc=6dabbf0a first=0:3182:2")

    def test_names_are_case_insensitive(self):
        build_image(self.path)
        line = expected_frag_line(self.path, "super mario galaxy [rmgp01]/rmgp01.ISO")
        self.assertIn("n=2 size=3", line)

    def test_missing_file(self):
        build_image(self.path)
        with self.assertRaises(FileNotFoundError):
            expected_frag_line(self.path, "Super Mario Galaxy [RMGP01]/RMGP02.iso")

    def test_read_file_follows_chain_and_size(self):
        build_image(self.path)
        data = read_file(self.path, "super mario galaxy [rmgp01]/RMGP01.iso")
        self.assertEqual(data, b"A" * 512 + b"B" * 512 + b"C" * (512 - 100))

    def test_read_file_missing(self):
        build_image(self.path)
        with self.assertRaises(FileNotFoundError):
            read_file(self.path, "Super Mario Galaxy [RMGP01]/nope.txt")

    def test_fat16_raises(self):
        build_image(self.path, fat16=True)
        with self.assertRaises(NotFat32Error):
            expected_frag_line(self.path, "x.iso")


if __name__ == "__main__":
    unittest.main()
