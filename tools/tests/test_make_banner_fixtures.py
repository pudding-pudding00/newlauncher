import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from make_banner_fixtures import (ADPCM_COEFS, TEX_BYTES, bns, brlan, brlyt, channel_banner_app,  # noqa: E402
                                  encode_adpcm, gc_bnr, gc_iso, imd5,
                                  main, pane, rgb5a3_pixel, sample_bnr, tex_bytes, tile_rgb5a3, tpl,
                                  u8_archive)


def sections(data: bytes) -> list:
    """(Magic, Länge) je Abschnitt einer BRLYT-Datei."""
    out, at = [], struct.unpack_from(">H", data, 0x0C)[0]
    for _ in range(struct.unpack_from(">H", data, 0x0E)[0]):
        magic, size = data[at:at + 4], struct.unpack_from(">I", data, at + 4)[0]
        out.append((magic, size))
        at += size
    return out


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

    def test_brlyt_sections_and_tree(self):
        root = pane("pan1", "RootPane", 0, 0, 608, 456, children=[pane("pic1", "logo", 10, 20, 40, 20, material=1)])
        data = brlyt(608, 456, ["a.tpl", "b.tpl"], [("bg", 0, None), ("logo", 1, (10, 20, 30, 40))], [root])
        self.assertEqual(data[:6], b"RLYT" + bytes([0xFE, 0xFF]))
        self.assertEqual(struct.unpack_from(">I", data, 8)[0], len(data))
        magics = [m for m, _ in sections(data)]
        self.assertEqual(magics, [b"lyt1", b"txl1", b"mat1", b"pan1", b"pas1", b"pic1", b"pae1"])
        self.assertTrue(all(size % 4 == 0 for _, size in sections(data)))
        self.assertEqual(struct.unpack_from(">ff", data, 0x10 + 0x0C), (608.0, 456.0))
        pic_at = data.index(b"pic1")
        self.assertEqual(struct.unpack_from(">I", data, pic_at + 4)[0], 0x60)
        self.assertEqual(struct.unpack_from(">H", data, pic_at + 0x5C)[0], 1)

    def test_brlan_header_and_keys(self):
        data = brlan(120, True, ["a.tpl"], [("logo", False, [("RLPA", [(0, 5, False, [(0, 0, 0), (60, 360, 0)])])]),
                                            ("m", True, [("RLTP", [(0, 0, True, [(0, 0, 0)])])])])
        self.assertEqual(data[:8], b"RLAN" + bytes([0xFE, 0xFF, 0x00, 0x08]))
        self.assertEqual(struct.unpack_from(">I", data, 8)[0], len(data))
        self.assertEqual(sections(data), [(b"pai1", len(data) - 0x10)])
        self.assertEqual(struct.unpack_from(">HBxHH", data, 0x18), (120, 1, 1, 2))
        self.assertIn(b"a.tpl" + bytes(1), data)
        self.assertIn(b"RLPA", data)
        self.assertIn(struct.pack(">fff", 60, 360, 0), data)
        self.assertIn(struct.pack(">fHxx", 0, 0), data)

    def test_adpcm_hand_frame_and_error(self):
        import math
        frames, decoded = encode_adpcm([2, 6, 4, -12, 2])
        self.assertEqual(len(frames), 8)
        self.assertEqual(decoded, [2, 6, 4, -12, 2])
        pcm = [int(12000 * math.sin(i * 0.07)) for i in range(14 * 20 + 3)]
        frames, decoded = encode_adpcm(pcm)
        self.assertEqual(len(frames), 8 * 21)
        self.assertEqual(len(decoded), len(pcm))
        self.assertLess(max(abs(a - b) for a, b in zip(pcm, decoded)), 600)

    def test_bns_layout(self):
        data = bns([[0] * 30, [100] * 30], 32000, True, 14)
        self.assertEqual(data[:8], b"BNS " + bytes([0xFE, 0xFF, 0x01, 0x00]))
        self.assertEqual(struct.unpack_from(">I", data, 8)[0], len(data))
        info_at, info_size, data_at, data_size = struct.unpack_from(">IIII", data, 16)
        self.assertEqual((data[info_at:info_at + 4], data[data_at:data_at + 4]), (b"INFO", b"DATA"))
        self.assertEqual(data_at + data_size, len(data))
        self.assertEqual(struct.unpack_from(">BBBxHxxIII", data, info_at + 8), (0, 1, 2, 32000, 14, 30, 0x18))
        ch1 = struct.unpack_from(">I", data, info_at + 8 + 0x18 + 4)[0]
        d1, a1 = struct.unpack_from(">II", data, info_at + 8 + ch1)
        self.assertEqual(d1, 3 * 8)
        self.assertEqual(struct.unpack_from(">16h", data, info_at + 8 + a1), ADPCM_COEFS)

    def test_channel_banner_app(self):
        data = channel_banner_app()
        self.assertEqual(data[0x80:0x84], b"IMET")
        self.assertEqual(data[0x640:0x644], bytes([0x55, 0xAA, 0x38, 0x2D]))
        inner_at = data.index(b"IMD5") + 32
        self.assertEqual(data[inner_at:inner_at + 4], bytes([0x55, 0xAA, 0x38, 0x2D]))
        for name in (b"banner.brlyt\0", b"bg.tpl\0", b"logo.tpl\0", b"RLYT", b"banner_Start.brlan\0",
                     b"banner_Loop.brlan\0", b"logo2.tpl\0", b"RLAN"):
            self.assertIn(name, data[inner_at:])
        self.assertIn(b"sound.bin\0", data)
        self.assertIn(b"BNS " + bytes([0xFE, 0xFF, 0x01, 0x00]), data)

    def test_main_writes_bnr(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(main(Path(d)), 0)
            self.assertEqual((Path(d) / "opening.bnr").read_bytes(), sample_bnr())
            self.assertEqual((Path(d) / "banner.app").read_bytes(), channel_banner_app())


if __name__ == "__main__":
    unittest.main()
