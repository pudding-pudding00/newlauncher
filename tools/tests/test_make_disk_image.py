"""Tests für tools/make_disk_image.py (M16 Task 2, Spec v2 15)."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_disk_image as mdi  # noqa: E402


def entry(mbr: bytes, i: int) -> tuple[int, int, int]:
    off = 446 + 16 * i
    return mbr[off + 4], struct.unpack_from("<I", mbr, off + 8)[0], struct.unpack_from("<I", mbr, off + 12)[0]


class LayoutTest(unittest.TestCase):
    def test_plan_aligns_to_1mib_in_given_order(self):
        parts = mdi.plan([("fat32", 64 * 2048), ("ntfs", 32 * 2048)])
        self.assertEqual(parts, [mdi.Part("fat32", 0x0C, 2048, 64 * 2048), mdi.Part("ntfs", 0x07, 2048 + 64 * 2048, 32 * 2048)])

    def test_plan_rounds_up_to_whole_mib(self):
        parts = mdi.plan([("ext4", 100), ("fat32", 64 * 2048)])
        self.assertEqual(parts[0].sectors, 2048)
        self.assertEqual(parts[1].lba, 4096)

    def test_build_mbr_entries_and_signature(self):
        mbr = mdi.build_mbr(mdi.plan([("ntfs", 2048), ("fat32", 4096), ("ext4", 2048)]))
        self.assertEqual(len(mbr), 512)
        self.assertEqual(mbr[510:], b"\x55\xaa")
        self.assertEqual(entry(mbr, 0), (0x07, 2048, 2048))
        self.assertEqual(entry(mbr, 1), (0x0C, 4096, 4096))
        self.assertEqual(entry(mbr, 2), (0x83, 8192, 2048))
        self.assertEqual(entry(mbr, 3), (0, 0, 0))

    def test_more_than_four_partitions_rejected(self):
        with self.assertRaises(ValueError):
            mdi.plan([("fat32", 2048)] * 5)

    def test_unknown_kind_rejected(self):
        with self.assertRaises(ValueError):
            mdi.parse_part("exfat")

    def test_parse_part_with_and_without_dir(self):
        self.assertEqual(mdi.parse_part("ntfs"), ("ntfs", None))
        self.assertEqual(mdi.parse_part("fat32:C:/x/sd"), ("fat32", Path("C:/x/sd")))

    def test_fat32_has_minimum_size(self):
        # FAT32 braucht mindestens 65525 Cluster; mit 512-Byte-Clustern also > 32 MiB.
        self.assertGreaterEqual(mdi.size_sectors("fat32", 0), 64 * 2048)
        self.assertGreater(mdi.size_sectors("fat32", 200 * 1024 * 1024), 200 * 2048)

    def test_format_script_mounts_ntfs_only_for_filling(self):
        script = mdi.format_script(mdi.plan([("fat32", 64 * 2048), ("ntfs", 32 * 2048)]), [True, True])
        self.assertIn("mkfs.fat -F 32", script)
        self.assertIn("mcopy", script)
        self.assertIn("mkntfs", script)
        self.assertIn(f"-p {2048 + 64 * 2048}", script)  # versteckte Sektoren = Partitionsbeginn
        self.assertIn("umount", script)
        self.assertNotIn("ntfsfix", script)


class AssembleTest(unittest.TestCase):
    def test_assemble_places_images_at_their_lba(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            parts = mdi.plan([("fat32", 2048), ("ntfs", 2048)])
            for i, fill in enumerate((b"\x11", b"\x22")):
                (tmp / f"p{i}.img").write_bytes(fill * (2048 * 512))
            out = tmp / "disk.raw"
            mdi.assemble(out, parts, tmp)
            data = out.read_bytes()
            self.assertEqual(data[510:512], b"\x55\xaa")
            self.assertEqual(data[2048 * 512], 0x11)
            self.assertEqual(data[4096 * 512 - 1], 0x11)
            self.assertEqual(data[4096 * 512], 0x22)
            self.assertEqual(len(data), (4096 + 2048 + mdi.ALIGN) * 512)

    def test_assemble_rejects_wrong_image_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            parts = mdi.plan([("fat32", 2048)])
            (tmp / "p0.img").write_bytes(b"\0" * 512)
            with self.assertRaises(ValueError):
                mdi.assemble(tmp / "disk.raw", parts, tmp)


if __name__ == "__main__":
    unittest.main()
