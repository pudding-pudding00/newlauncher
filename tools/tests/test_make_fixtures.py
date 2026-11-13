import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from deploy_dolphin import deploy  # noqa: E402
from make_fixtures import main as make_fixtures  # noqa: E402
from metabin import read_meta_bin  # noqa: E402
from pngfile import PNG_SIGNATURE  # noqa: E402


class MakeFixturesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.out = Path(self._tmp.name) / "sd"
        self.assertEqual(make_fixtures(self.out), 0)

    def tearDown(self):
        self._tmp.cleanup()

    def test_expected_files_exist(self):
        for rel in ["newlauncher/config.ini", "newlauncher/favorites.ini", "newlauncher/history.ini",
                    "newlauncher/cache/meta.bin", "roms/nes/Super Mario Bros. (World).nes",
                    "roms/nes/readme.txt", "roms/snes/F-Zero (Europe).SMC", "roms/gbc/.keep",
                    "games/Metroid Prime [GM8P01]/game.iso", "wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs",
                    "wbfs/Super Mario Galaxy [RMGP01]/RMGP01.iso", "wbfs/Kaputt [XXXX01]/XXXX01.wbfs"]:
            self.assertTrue((self.out / rel).is_file(), rel)
        roms = [p for p in (self.out / "roms").rglob("*") if p.is_file() and p.suffix.lower() not in (".txt", ".keep", "")]
        self.assertEqual(len(roms), 9)

    def test_emulators_ini_points_all_consoles_to_argv_echo(self):
        text = (self.out / "newlauncher/emulators.ini").read_text(encoding="utf-8")
        for console in ["nes", "snes", "gb", "gbc", "gba"]:
            self.assertIn(f"[{console}]\ndol = sd:/apps/argv_echo/boot.dol\nargs = {{dir}}|{{file}}\n", text)

    def test_n64_paths(self):
        emus = (self.out / "newlauncher/emulators.ini").read_text(encoding="utf-8")
        self.assertIn("[n64_not64]\ndol = sd:/apps/argv_echo/boot.dol\nargs = {path}\n", emus)
        self.assertNotIn("[n64_wii64]", emus)  # Standard-DOL fehlt in den Fixtures absichtlich
        vc = (self.out / "newlauncher/n64_vc.ini").read_text(encoding="utf-8")
        self.assertEqual(vc, "[map]\nn64/Mario Kart 64 (Europe).z64 = 000100014E4C5443\n")

    def test_config_uses_sd_library(self):
        text = (self.out / "newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("library = sd", text)
        self.assertNotIn("library = usb", text)

    def test_disc_headers_at_right_offsets(self):
        gc = (self.out / "games/Metroid Prime [GM8P01]/game.iso").read_bytes()
        self.assertEqual(gc[:6], b"GM8P01")
        self.assertEqual(struct.unpack_from(">I", gc, 0x1C)[0], 0xC2339F3D)
        wii = (self.out / "wbfs/Super Mario Galaxy [RMGP01]/RMGP01.iso").read_bytes()
        self.assertEqual(struct.unpack_from(">I", wii, 0x18)[0], 0x5D1C9EA3)
        self.assertTrue(wii[0x20:].startswith(b"Super Mario Galaxy"))
        wbfs = (self.out / "wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs").read_bytes()
        self.assertEqual(wbfs[:4], b"WBFS")
        self.assertEqual(wbfs[8], 9)
        self.assertEqual(wbfs[512:518], b"RMCP01")
        broken = (self.out / "wbfs/Kaputt [XXXX01]/XXXX01.wbfs").read_bytes()
        self.assertNotEqual(broken[:4], b"WBFS")

    def test_meta_bin_readable(self):
        meta = read_meta_bin(self.out / "newlauncher/cache/meta.bin")
        self.assertEqual(meta["RMCP01"][0], "Mario Kart Wii")
        self.assertEqual(meta["n64/super mario 64 (europe) (en,fr,de)"][1], 1996)

    def test_favorites_and_history(self):
        fav = (self.out / "newlauncher/favorites.ini").read_text(encoding="utf-8")
        self.assertIn("[favorites]\nRMCP01\n", fav)
        hist = (self.out / "newlauncher/history.ini").read_text(encoding="utf-8")
        self.assertIn("1762700000 = n64/Super Mario 64 (Europe) (En,Fr,De).z64", hist)

    def test_cheat_list_for_gc_game(self):
        # Selbst erzeugte Gecko-Liste (M15): 3 Codes (einer mit Platzhalter), 1 kaputter Block ohne Codezeilen.
        text = (self.out / "newlauncher/codes/GM8P01.txt").read_text(encoding="utf-8")
        self.assertTrue(text.startswith("GM8P01\nMetroid Prime\n\n"))
        self.assertIn("Unendlich Energie [Fixture]\n04001000 00000063\n", text)
        self.assertIn("0400XXXX", text)
        self.assertIn("\nNur Name\n\n", text)

    def test_deploy_copies_fixtures(self):
        sd = Path(self._tmp.name) / "deployed"
        dist = Path(self._tmp.name) / "dist"
        dist.mkdir()
        deploy(None, sd, dist, fixtures_dir=self.out)
        self.assertTrue((sd / "roms/n64/Mario Kart 64 (Europe).z64").is_file())
        self.assertTrue((sd / "newlauncher/config.ini").is_file())

    def test_cover_sources(self):
        src = self.out / "newlauncher/covers_src"
        for rel, size in [("nes/Super Mario Bros. (World).png", (128, 180)),
                          ("n64/Mario Kart 64 (Europe).png", (300, 300))]:
            data = (src / rel).read_bytes()
            self.assertEqual(data[:8], PNG_SIGNATURE, rel)
            self.assertEqual(struct.unpack_from(">II", data, 16), size, rel)
        junk = (src / "gb/Tetris (World) (Rev 1).png").read_bytes()
        self.assertEqual(len(junk), 40)
        self.assertNotEqual(junk[:8], PNG_SIGNATURE)

    def test_deploy_clears_cover_cache_with_fixtures(self):
        sd = Path(self._tmp.name) / "deployed"
        dist = Path(self._tmp.name) / "dist"
        dist.mkdir()
        stale_tex = sd / "newlauncher/cache/covers/nes/alt.tex"
        stale_src = sd / "newlauncher/covers_src/nes/alt.png"
        keep = sd / "newlauncher/cache/meta_alt.bin"
        for p in (stale_tex, stale_src, keep):
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"x")
        deploy(None, sd, dist)  # ohne Fixtures bleibt alles stehen
        self.assertTrue(stale_tex.is_file())
        self.assertTrue(stale_src.is_file())
        deploy(None, sd, dist, fixtures_dir=self.out)
        self.assertFalse(stale_tex.exists())
        self.assertFalse(stale_src.exists())
        self.assertTrue(keep.is_file())
        self.assertTrue((sd / "newlauncher/covers_src/nes/Super Mario Bros. (World).png").is_file())


if __name__ == "__main__":
    unittest.main()
