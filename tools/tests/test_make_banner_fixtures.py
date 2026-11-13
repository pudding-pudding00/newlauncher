import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from make_banner_fixtures import (gc_bnr, gc_iso, main, rgb5a3_pixel, sample_bnr,  # noqa: E402
                                  tile_rgb5a3)


class MakeBannerFixturesTest(unittest.TestCase):
    def test_bnr2_layout(self):
        bnr = sample_bnr()
        self.assertEqual(bnr[:4], b"BNR2")
        self.assertEqual(len(bnr), 0x1820 + 6 * 0x140)
        de = 0x1820 + 0x140
        self.assertEqual(bnr[de + 0x40:de + 0x40 + 14], b"Fixture-Banner")

    def test_bnr1_has_one_block(self):
        self.assertEqual(len(gc_bnr([("a", "b", "c", "d", "e")], bnr2=False)), 0x1820 + 0x140)

    def test_pixel_and_tiling(self):
        self.assertEqual(rgb5a3_pixel(255, 0, 0), 0xFC00)
        px = list(range(8 * 4))
        tiled = tile_rgb5a3(px, 8, 4)
        # zweite Kachel beginnt mit Pixel (4, 0) = Index 4
        self.assertEqual(struct.unpack_from(">H", tiled, 32)[0], 4)

    def test_iso_fst_points_to_banner(self):
        header = bytearray(0x440)
        header[0:6] = b"GM8P01"
        image = gc_iso(bytes(header), [("opening.bnr", sample_bnr())])
        fst_at, fst_size = struct.unpack_from(">II", image, 0x424)
        count = struct.unpack_from(">I", image, fst_at + 8)[0]
        self.assertEqual(count, 2)
        _, off, size = struct.unpack_from(">III", image, fst_at + 12)
        self.assertEqual(image[off:off + size], sample_bnr())
        names = image[fst_at + count * 12:fst_at + fst_size]
        self.assertEqual(names, b"opening.bnr\0")

    def test_main_writes_bnr(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(main(Path(d)), 0)
            self.assertEqual((Path(d) / "opening.bnr").read_bytes(), sample_bnr())


if __name__ == "__main__":
    unittest.main()
