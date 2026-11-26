"""Prüft das vom Host-Test geschriebene ZIP (ZipWriter, M26) mit Pythons zipfile."""

import pathlib
import unittest
import zipfile

SAMPLE = pathlib.Path(__file__).resolve().parents[2] / "build" / "host" / "zip_writer_sample.zip"


class ZipCompatTest(unittest.TestCase):
    def test_sample_opens_in_zipfile(self):
        if not SAMPLE.exists():
            self.skipTest("Host-Tests noch nicht gelaufen")
        with zipfile.ZipFile(SAMPLE) as z:
            self.assertIsNone(z.testzip())
            self.assertEqual(z.namelist(), ["newlauncher-save.ini", "a/b.bin", "c.raw"])
            self.assertEqual(z.read("a/b.bin"), b"Spielstand " * 100)
            self.assertEqual(z.read("c.raw"), bytes(5000))
            self.assertEqual(z.getinfo("a/b.bin").compress_type, zipfile.ZIP_DEFLATED)
            self.assertEqual(z.getinfo("a/b.bin").date_time, (2025, 11, 26, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
