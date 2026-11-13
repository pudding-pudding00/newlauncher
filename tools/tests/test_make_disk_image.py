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


class WbfsTest(unittest.TestCase):
    """M16 Task 5: WBFS-Partition direkt in Python, Layout wie libwbfs (hd 512, Block 2 MiB)."""

    def _iso(self, path: Path, id6: str, size: int) -> bytes:
        head = bytearray(512)
        head[:6] = id6.encode()
        head[0x18:0x1C] = bytes.fromhex("5D1C9EA3")
        data = bytes(head) + bytes(((i * 7) & 0xFF) or 1 for i in range(size - 512))
        path.write_bytes(data)
        return data

    def test_wbfs_is_known_kind(self):
        self.assertEqual(mdi.parse_part("wbfs:x"), ("wbfs", Path("x")))
        self.assertEqual(mdi.size_sectors("wbfs", 0), 64 * 2048)

    def test_wbfs_image_has_head_slots_and_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "in"
            src.mkdir()
            a = self._iso(src / "00_RNLA01.iso", "RNLA01", 3 << 20)
            self._iso(src / "01_RNLB01.broken.iso", "RNLB01", 1 << 20)
            (src / "nl_ntfs.txt").write_text("keine ISO")
            sectors = 64 * 2048
            mdi.write_wbfs_image(tmp / "p.img", sectors, src)
            d = (tmp / "p.img").read_bytes()
            self.assertEqual(len(d), sectors * 512)
            self.assertEqual(d[:4], b"WBFS")
            self.assertEqual(struct.unpack_from(">I", d, 4)[0], sectors)
            self.assertEqual((d[8], d[9]), (9, 21))
            self.assertEqual(d[12:15], bytes([1, 1, 0]))
            info0 = d[512:512 + 19 * 512]  # disc_info_lba = 19
            self.assertEqual(info0[:6], b"RNLA01")
            wlba0 = struct.unpack_from(">3H", info0, 0x100)
            self.assertEqual(wlba0, (1, 2, 0))  # 3 MiB → zwei Blöcke zu 2 MiB
            block = 2 << 20
            self.assertEqual(d[block:block + (3 << 20)], a)
            info1 = d[(1 + 19) * 512:(1 + 2 * 19) * 512]
            self.assertEqual(info1[:6], b"RNLB01")
            self.assertEqual(info1[0x18:0x1C], bytes(4))  # .broken: Disc-Kopf ohne Wii-Magic
            self.assertEqual(struct.unpack_from(">H", info1, 0x100)[0], 3)

    def test_format_script_leaves_wbfs_to_python(self):
        script = mdi.format_script(mdi.plan([("fat32", 64 * 2048), ("wbfs", 64 * 2048)]), [True, True])
        self.assertNotIn("p1.img", script)

    def test_wbfs_rejects_too_many_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / "in"
            src.mkdir()
            self._iso(src / "00_RNLA01.iso", "RNLA01", 3 << 20)
            with self.assertRaises(ValueError):
                mdi.write_wbfs_image(tmp / "p.img", 2 * 4096, src)  # zwei Blöcke: Kopf + einer


if __name__ == "__main__":
    unittest.main()
