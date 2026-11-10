import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import prepare_covers as pc  # noqa: E402
from pngfile import PNG_SIGNATURE, solid_png, write_png  # noqa: E402


class PngFileTest(unittest.TestCase):
    def test_solid_png_is_valid(self):
        data = solid_png(4, 2, (255, 0, 0, 255))
        self.assertEqual(data[:8], PNG_SIGNATURE)
        length, kind = struct.unpack_from(">I4s", data, 8)
        self.assertEqual((length, kind), (13, b"IHDR"))
        w, h, depth, color = struct.unpack_from(">IIBB", data, 16)
        self.assertEqual((w, h, depth, color), (4, 2, 8, 6))
        idat_at = data.index(b"IDAT")
        (idat_len,) = struct.unpack_from(">I", data, idat_at - 4)
        raw = zlib.decompress(data[idat_at + 4:idat_at + 4 + idat_len])
        self.assertEqual(len(raw), 2 * (1 + 16))
        self.assertEqual(raw[1:5], bytes([255, 0, 0, 255]))
        self.assertTrue(data.endswith(b"IEND\xaeB`\x82"))

    def test_write_png_creates_dirs(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a" / "b.png"
            write_png(path, 1, 1, bytes([1, 2, 3, 4]))
            self.assertEqual(path.read_bytes()[:8], PNG_SIGNATURE)


class PrepareCoversTest(unittest.TestCase):
    def test_thumbnail_name_replaces_reserved(self):
        self.assertEqual(pc.thumbnail_name("Tom & Jerry: A/B"), "Tom _ Jerry_ A_B")
        self.assertEqual(pc.thumbnail_name('a*b?c<d>e|f"g\\h`i'), "a_b_c_d_e_f_g_h_i")

    def test_thumbnail_url_quotes(self):
        self.assertEqual(
            pc.thumbnail_url("n64", "Super Mario 64 (Europe) (En,Fr,De)"),
            "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%2064/Named_Boxarts/"
            "Super%20Mario%2064%20%28Europe%29%20%28En%2CFr%2CDe%29.png")

    def test_main_downloads_skips_and_counts(self):
        png = solid_png(2, 2, (0, 0, 255, 255))
        calls = []

        def fetch(url):
            calls.append(url)
            if "Super%20Mario" in url:
                return png
            if "Zelda" in url:
                return b"<html>not found</html>"
            return None

        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp)
            for rel in ["roms/nes/Super Mario Bros. (World).nes", "roms/nes/Zelda (Europe).nes",
                        "roms/gb/Tetris (World).gb", "roms/gb/notes.txt"]:
                (lib / rel).parent.mkdir(parents=True, exist_ok=True)
                (lib / rel).write_bytes(b"")
            counts = pc.main([str(lib)], fetch=fetch)
            target = lib / "newlauncher/covers_src/nes/Super Mario Bros. (World).png"
            self.assertEqual(target.read_bytes(), png)
            self.assertFalse((lib / "newlauncher/covers_src/nes/Zelda (Europe).png").exists())
            self.assertEqual(counts, {"ok": 1, "vorhanden": 0, "fehlt": 2})
            self.assertEqual(len(calls), 3)

            calls.clear()
            counts = pc.main([str(lib)], fetch=fetch)
            self.assertEqual(counts, {"ok": 0, "vorhanden": 1, "fehlt": 2})
            self.assertEqual(len(calls), 2)

            counts = pc.main([str(lib), "--force"], fetch=fetch)
            self.assertEqual(counts["ok"], 1)


if __name__ == "__main__":
    unittest.main()
