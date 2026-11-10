import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from wii_title import (TEST_CHANNEL_ID, TEST_CHANNEL_NAMES, imet_banner, install_channel,  # noqa: E402
                       ticket_bytes, tmd_bytes)


class WiiTitleTest(unittest.TestCase):
    def test_tmd_names_a_bootable_ios(self):
        # ES_Launch bootet erst ins IOS aus 0x184; IOS 0 gibt es nicht, Dolphin bliebe dort hängen.
        tmd = tmd_bytes(TEST_CHANNEL_ID, [b"x"], boot_index=0)
        self.assertEqual(struct.unpack_from(">Q", tmd, 0x184)[0], 0x000000010000003A)

    def test_tmd_offsets(self):
        contents = [b"banner" * 10, b"\x00\x00\x01\x00" * 64]
        tmd = tmd_bytes(TEST_CHANNEL_ID, contents, boot_index=1)
        self.assertEqual(struct.unpack_from(">I", tmd, 0)[0], 0x00010001)
        self.assertEqual(struct.unpack_from(">Q", tmd, 0x18C)[0], TEST_CHANNEL_ID)
        self.assertEqual(struct.unpack_from(">H", tmd, 0x1DE)[0], 2)
        self.assertEqual(struct.unpack_from(">H", tmd, 0x1E0)[0], 1)
        cid, index, ctype, size = struct.unpack_from(">IHHQ", tmd, 0x1E4 + 36)
        self.assertEqual((cid, index, ctype, size), (1, 1, 1, len(contents[1])))
        self.assertEqual(tmd[0x1E4 + 36 + 16:0x1E4 + 36 + 36], hashlib.sha1(contents[1]).digest())
        self.assertEqual(len(tmd), 0x1E4 + 2 * 36)

    def test_ticket_title_id(self):
        tik = ticket_bytes(TEST_CHANNEL_ID)
        self.assertEqual(len(tik), 0x2A4)
        self.assertEqual(struct.unpack_from(">I", tik, 0)[0], 0x00010001)
        self.assertEqual(struct.unpack_from(">Q", tik, 0x1DC)[0], TEST_CHANNEL_ID)

    def test_imet_names(self):
        data = imet_banner({1: "Hello", 2: "Hallo"})
        self.assertEqual(len(data), 0x600)
        self.assertEqual(data[0x40:0x44], b"IMET")
        en = 0x5C + 42 * 2
        de = 0x5C + 2 * 42 * 2
        self.assertEqual(data[en:en + 10], "Hello".encode("utf-16-be"))
        self.assertEqual(data[en + 10:en + 84], bytes(74))
        self.assertEqual(data[de:de + 10], "Hallo".encode("utf-16-be"))
        self.assertEqual(data[0x5C:0x5C + 84], bytes(84))  # Japanisch leer
        with self.assertRaises(ValueError):
            imet_banner({1: "x" * 43})

    def test_install_channel_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "Wii"
            boot = b"\x00\x00\x01\x00" + bytes(252)
            install_channel(root, TEST_CHANNEL_ID, TEST_CHANNEL_NAMES, boot)
            base = root / "title/00010001/4e4c5443"
            for rel in ["content/title.tmd", "content/00000000.app", "content/00000001.app"]:
                self.assertTrue((base / rel).is_file(), rel)
            self.assertTrue((base / "data").is_dir())
            self.assertTrue((root / "ticket/00010001/4e4c5443.tik").is_file())
            self.assertEqual((base / "content/00000001.app").read_bytes(), boot)
            tmd = (base / "content/title.tmd").read_bytes()
            self.assertEqual(struct.unpack_from(">H", tmd, 0x1E0)[0], 1)


if __name__ == "__main__":
    unittest.main()
