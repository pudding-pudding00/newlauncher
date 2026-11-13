import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from make_banner_fixtures import (TEX_BYTES, gc_bnr, gc_iso, imd5, main, rgb5a3_pixel,  # noqa: E402
                                  sample_bnr, tex_bytes, tile_rgb5a3, tpl, u8_archive)


class MakeBannerFixturesTest(unittest.TestCase):
    def test_u8_layout(self):
        arc = u8_archive({"meta": {"banner.bin": b""}, "icon.bin": b"	"})
        self.assertEqual(arc[:4], bytes([0x55, 0xAA, 0x38, 0x2D]))
        root, header, data = struct.unpack_from(">III", arc, 4)
        self.assertEqual(root, 0x20)
        self.assertEqual(data % 32, 0)
        self.assertEqual(struct.unpack_from(">I", arc, 0x20 + 8)[0], 4)  # Wurzel, meta, banner.bin, icon.bin
        typ_meta, parent, nxt = struct.unpack_from(">III", arc, 0x20 + 12)
        self.assertEqual((typ_meta >> 24, parent, nxt), (1, 0, 3))
        _, off, size = struct.unpack_from(">III", arc, 0x20 + 24)
        self.assertEqual(arc[off:off + size], b"")

    def test_tpl_all_formats(self):
        def px(x, y):
            return (x * 36, y * 36, 128, 255)
        for fmt in TEX_BYTES:
            pal = list(range(16)) if fmt in (8, 9, 10) else None
            fn = (lambda x, y: (x + y) % 16) if pal else px
            data = tpl([(fmt, 8, 8, fn, pal)])
            self.assertEqual(struct.unpack_from(">III", data, 0), (0x0020AF30, 1, 0x0C))
            hdr, pal_hdr = struct.unpack_from(">II", data, 0x0C)
            h, w, f, at = struct.unpack_from(">HHII", data, hdr)
            self.assertEqual((h, w, f), (8, 8, fmt))
            self.assertEqual(at % 32, 0)
            self.assertEqual(len(data), at + tex_bytes(fmt, 8, 8), fmt)
            self.assertEqual(pal_hdr != 0, pal is not None)

    def test_tpl_rgba8_and_i8_bytes(self):
        data = tpl([(6, 4, 4, lambda x, y: (1, 2, 3, 4), None)])
        at = struct.unpack_from(">I", data, struct.unpack_from(">I", data, 0x0C)[0] + 8)[0]
        self.assertEqual(data[at:at + 2], bytes([4, 1]))         # A, R
        self.assertEqual(data[at + 32:at + 34], bytes([2, 3]))   # G, B
        data = tpl([(1, 8, 4, lambda x, y: (x * 10 + y, 0, 0, 255), None)])
        at = struct.unpack_from(">I", data, struct.unpack_from(">I", data, 0x0C)[0] + 8)[0]
        self.assertEqual(data[at + 9], 11)                       # (1, 1) in der Kachel 8×4
        self.assertEqual(tex_bytes(0, 10, 6), 64)
        self.assertEqual(tex_bytes(6, 10, 6), 384)

    def test_imd5_header(self):
        wrapped = imd5(b"abc")
        self.assertEqual(wrapped[:4], b"IMD5")
        self.assertEqual(struct.unpack_from(">I", wrapped, 4)[0], 3)
        self.assertEqual(wrapped[32:], b"abc")
        self.assertEqual(len(wrapped[16:32]), 16)

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
