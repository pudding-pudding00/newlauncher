"""Tests for tools/build_channel.py (M30): IMET header generation and Wii/vWii staging."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import build_channel  # noqa: E402


class BuildChannelTest(unittest.TestCase):
    def test_imet_header_has_valid_magic_and_md5(self):
        imet = build_channel.build_imet_header("newlauncher")
        self.assertEqual(len(imet), build_channel.IMET_SIZE)
        self.assertEqual(imet[0x40:0x44], b"IMET")
        self.assertTrue(build_channel.verify_imet_header(imet))
        corrupt = bytearray(imet)
        corrupt[0x60] ^= 0xFF
        self.assertFalse(build_channel.verify_imet_header(bytes(corrupt)))

    def test_stage_channel_writes_manifest_and_validates_inputs(self):
        with tempfile.TemporaryDirectory() as d:
            tmp = Path(d)
            dol = tmp / "forwarder.dol"
            dol.write_bytes(b"\x00" * 0x200)
            out = tmp / "staged"
            manifest_path = build_channel.stage_channel("wii", dol, out)
            data = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(data["title_id"], "00010001554E4C41")
            self.assertEqual(data["fourcc"], "UNLA")
            self.assertEqual(data["target"], "wii")
            self.assertTrue((out / "00000000.app").is_file())
            self.assertTrue(build_channel.verify_imet_header((out / "00000000.app").read_bytes()))

            with self.assertRaises(ValueError):
                build_channel.stage_channel("gc", dol, out)
            bad_dol = tmp / "tiny.dol"
            bad_dol.write_bytes(b"\x00" * 16)
            with self.assertRaises(ValueError):
                build_channel.stage_channel("vwii", bad_dol, out)


if __name__ == "__main__":
    unittest.main()
