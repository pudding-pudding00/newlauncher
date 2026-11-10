import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import prepare_meta as pm  # noqa: E402
from make_fixtures import disc_header, wbfs_file  # noqa: E402
from metabin import meta_key, read_meta_bin  # noqa: E402

WIITDB = """<?xml version="1.0" encoding="UTF-8"?>
<datafile>
  <WiiTDB version="1" games="2"/>
  <game name="Mario Kart Wii">
    <id>RMCP01</id>
    <locale lang="EN"><title>Mario Kart Wii</title><synopsis>Race.</synopsis></locale>
    <locale lang="DE"><title>Mario Kart Wii (DE)</title><synopsis>Rennen.</synopsis></locale>
    <date year="2008" month="4" day="11"/>
    <input players="4"><control type="wiimote" required="true"/></input>
  </game>
  <game name="Metroid Prime">
    <id>GM8P01</id>
    <locale lang="EN"><title>Metroid Prime</title><synopsis>Samus.</synopsis></locale>
    <date year="2002" month="" day=""/>
    <input players="1"/>
  </game>
  <game name="Unwanted">
    <id>ZZZZ01</id>
    <locale lang="EN"><title>Nope</title></locale>
  </game>
</datafile>
"""

YEAR_DAT = """clrmamepro (
\tname "Nintendo - Nintendo Entertainment System"
)

game (
\tcomment "Super Mario Bros. (World)"
\treleaseyear "1985"
\trom ( crc 3337EC46 )
)

game (
\tcomment "Tetris & Co: Edition"
\treleaseyear "1989"
\trom ( crc DEADBEEF )
)
"""

USERS_DAT = """clrmamepro (
\tname "Nintendo - Nintendo Entertainment System"
)

game (
\tcomment "Super Mario Bros. (World)"
\tusers 2
\trom ( crc 3337EC46 )
)
"""


def make_library(root: Path) -> None:
    def put(rel: str, data: bytes) -> None:
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    put("wbfs/X [RMCP01]/RMCP01.wbfs", wbfs_file("RMCP01", "MARIO KART WII"))
    put("games/Y [GM8P01]/game.iso", bytes(disc_header("GM8P01", "Metroid Prime", wii=False)))
    put("roms/nes/Super Mario Bros. (World).nes", b"")
    put("roms/nes/Renamed.NES", b"crc-match")
    put("roms/nes/Unknown Game (Europe) [!].nes", b"x")
    put("roms/nes/readme.txt", b"no rom")


class PrepareMetaTest(unittest.TestCase):
    def test_scan_library_finds_disc_ids_and_roms(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp)
            make_library(lib)
            keys = {key: console for key, console, _ in pm.scan_library(lib)}
            self.assertEqual(keys["RMCP01"], "wii")
            self.assertEqual(keys["GM8P01"], "gc")
            self.assertEqual(keys["nes/Super Mario Bros. (World).nes"], "nes")
            self.assertIn("nes/Renamed.NES", keys)
            self.assertNotIn("nes/readme.txt", keys)

    def test_parse_wiitdb_picks_language_with_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "wiitdb.xml"
            path.write_text(WIITDB, encoding="utf-8")
            meta = pm.parse_wiitdb(path, {"RMCP01", "GM8P01"}, "DE")
            self.assertEqual(meta["RMCP01"], ("Mario Kart Wii (DE)", 2008, 4, "Rennen."))
            self.assertEqual(meta["GM8P01"], ("Metroid Prime", 2002, 1, "Samus."))
            self.assertNotIn("ZZZZ01", meta)

    def test_parse_dat_reads_comment_value_and_crc(self):
        years = pm.parse_dat(YEAR_DAT, "releaseyear")
        self.assertEqual(years["Super Mario Bros. (World)"], ("1985", "3337EC46"))
        self.assertEqual(years["Tetris & Co: Edition"], ("1989", "DEADBEEF"))
        users = pm.parse_dat(USERS_DAT, "users")
        self.assertEqual(users["Super Mario Bros. (World)"], ("2", "3337EC46"))

    def test_retro_title_strips_tags(self):
        self.assertEqual(pm.retro_title("Super Mario 64 (Europe) (En,Fr,De)"), "Super Mario 64")
        self.assertEqual(pm.retro_title("Zelda [!]"), "Zelda")
        self.assertEqual(pm.retro_title("Super Mario Bros. (World)"), "Super Mario Bros.")
        self.assertEqual(pm.retro_title("(Plain)"), "(Plain)")  # nichts übrig → Original

    def test_build_entries_matches_by_name_then_crc(self):
        crc = f"{zlib.crc32(b'crc-match'):08X}"
        dat = YEAR_DAT.replace("DEADBEEF", crc)
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp)
            make_library(lib)
            games = pm.scan_library(lib)
            dats = {"nes": {"releaseyear": pm.parse_dat(dat, "releaseyear"),
                            "users": pm.parse_dat(USERS_DAT, "users")}}
            wii = {"RMCP01": ("Mario Kart Wii", 2008, 4, "Race.")}
            entries = pm.build_entries(games, wii, dats)
        self.assertEqual(entries["RMCP01"], ("Mario Kart Wii", 2008, 4, "Race."))
        self.assertNotIn("GM8P01", entries)  # kein wiitdb-Eintrag: Disc-Titel bleibt
        self.assertEqual(entries[meta_key("nes/Super Mario Bros. (World).nes")], ("Super Mario Bros.", 1985, 2, ""))
        self.assertEqual(entries[meta_key("nes/Renamed.NES")], ("Tetris & Co: Edition", 1989, 0, ""))
        self.assertEqual(entries[meta_key("nes/Unknown Game (Europe) [!].nes")], ("Unknown Game", 0, 0, ""))

    def test_main_writes_meta_bin(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / "lib"
            make_library(lib)
            xml = Path(tmp) / "wiitdb.xml"
            xml.write_text(WIITDB, encoding="utf-8")
            dat_dir = Path(tmp) / "dats"
            (dat_dir / "releaseyear").mkdir(parents=True)
            (dat_dir / "releaseyear" / "Nintendo - Nintendo Entertainment System.dat").write_text(YEAR_DAT)
            rc = pm.main([str(lib), "--wiitdb", str(xml), "--dat-dir", str(dat_dir), "--offline", "--lang", "EN"])
            self.assertEqual(rc, 0)
            meta = read_meta_bin(lib / "newlauncher/cache/meta.bin")
        self.assertEqual(meta["RMCP01"][0], "Mario Kart Wii")
        self.assertEqual(meta["GM8P01"][1], 2002)
        self.assertEqual(meta[meta_key("nes/Super Mario Bros. (World).nes")][1], 1985)

    def test_fetch_dats_skips_existing_and_offline(self):
        calls = []

        def fetch(url):
            calls.append(url)
            return b"game ( comment \"A\" releaseyear \"1990\" )" if "Game%20Boy.dat" in url else None

        with tempfile.TemporaryDirectory() as tmp:
            dat_dir = Path(tmp)
            existing = dat_dir / "releaseyear" / "Nintendo - Nintendo 64.dat"
            existing.parent.mkdir(parents=True)
            existing.write_text("old")
            pm.fetch_dats(dat_dir, fetch)
            self.assertEqual(existing.read_text(), "old")
            self.assertFalse(any("Nintendo%2064.dat" in u and "releaseyear" in u for u in calls))
            self.assertTrue((dat_dir / "releaseyear" / "Nintendo - Game Boy.dat").is_file())
            self.assertFalse((dat_dir / "maxusers" / "Nintendo - Nintendo 64.dat").exists())
            self.assertEqual(len(calls), 11)
            self.assertTrue(all(u.startswith(pm.DAT_BASE) for u in calls))


if __name__ == "__main__":
    unittest.main()
