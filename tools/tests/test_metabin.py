import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from metabin import meta_key, read_meta_bin, write_meta_bin  # noqa: E402


class MetaBinTest(unittest.TestCase):
    def test_round_trip_and_sorting(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "meta.bin"
            entries = {
                "n64/b": ("B", 1999, 2, "Text B"),
                "RMCP01": ("Mario Kart Wii", 2008, 4, "Rennen"),
                "n64/ä": ("Ä", 0, 0, ""),
            }
            write_meta_bin(path, entries)
            data = path.read_bytes()
            self.assertEqual(data[:4], b"NLMD")
            self.assertEqual(read_meta_bin(path), entries)
            keys = list(read_meta_bin(path).keys())
            self.assertEqual(keys, sorted(keys, key=lambda k: k.encode("utf-8")))

    def test_meta_key_lowers_ascii_only(self):
        self.assertEqual(meta_key("n64/Super Mario 64 (Europe).z64"), "n64/super mario 64 (europe)")
        self.assertEqual(meta_key("gb/Ärger.gb"), "gb/Ärger")
        self.assertEqual(meta_key("RMCP01"), "RMCP01")


if __name__ == "__main__":
    unittest.main()
