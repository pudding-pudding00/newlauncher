"""Tests für save_smoke.py: Spielstand-Fixtures für die Dolphin-Smokes (M26)."""

import hashlib
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from save_smoke import (  # noqa: E402
    BYTES_A,
    BYTES_B,
    EXTRA_NAME,
    cleanup,
    content_hash,
    fixture,
    journal_text,
    parse_backup_line,
    post_check,
    prepare,
    verify_zip,
)


def make_zip(files: dict[str, bytes], unit: str = "wii-RMGP", hash_: str | None = None) -> bytes:
    meta = ("[save]\nformat = 1\n"
            f"unit = {unit}\ntitle = T\ndevice = Wii\nsaved = 0\n"
            f"hash = {hash_ if hash_ is not None else content_hash(files)}\nfiles = {len(files)}\n")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("newlauncher-save.ini", meta)
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


class ContentHashTest(unittest.TestCase):
    def test_matches_launcher_definition(self):
        # sortiert nach Pfad, je Datei "pfad\ngröße\nsha256hex\n"
        files = {"b.sav": b"xy", "a.sav": b""}
        lines = (f"a.sav\n0\n{hashlib.sha256(b'').hexdigest()}\n"
                 f"b.sav\n2\n{hashlib.sha256(b'xy').hexdigest()}\n")
        self.assertEqual(content_hash(files), hashlib.sha256(lines.encode()).hexdigest())


class FixtureTest(unittest.TestCase):
    def setUp(self):
        self.user = Path("/u")

    def test_gb(self):
        fx = fixture("gb", self.user)
        self.assertEqual(fx.key, "gb/Tetris (World) (Rev 1).gb")
        self.assertEqual(fx.launcher_dir, "sd:/saves_smoke/gb")
        self.assertEqual(fx.file_name, "Tetris (World) (Rev 1).sav")
        self.assertEqual(fx.sd_rel, "saves_smoke/gb")
        self.assertEqual(fx.host_dir, self.user / "SDSync/saves_smoke/gb")

    def test_nand_lives_in_dolphin_nand(self):
        fx = fixture("nand", self.user)
        self.assertEqual(fx.key, "RMGP01")
        self.assertEqual(fx.launcher_dir, "nand:/title/00010000/524d4750/data")
        self.assertIsNone(fx.sd_rel)
        self.assertEqual(fx.host_dir, self.user / "Wii/title/00010000/524d4750/data")

    def test_emunand(self):
        fx = fixture("emunand", self.user)
        self.assertEqual(fx.launcher_dir, "sd:/emunand/title/00010000/524d4750/data")
        self.assertEqual(fx.sd_rel, "emunand/title/00010000/524d4750/data")


class JournalTest(unittest.TestCase):
    def test_writing_phase_with_added_file(self):
        text = journal_text("wii-RMGP", "nand:/title/00010000/524d4750/data",
                            "sd:/newlauncher/saves/wii-RMGP/x.zip", [EXTRA_NAME])
        self.assertEqual(text, "[restore]\nunit = wii-RMGP\ndir = nand:/title/00010000/524d4750/data\n"
                               "backup = sd:/newlauncher/saves/wii-RMGP/x.zip\nphase = writing\n"
                               f"added = {EXTRA_NAME}\n")


class ParseBackupLineTest(unittest.TestCase):
    def test_finds_unit_and_path(self):
        log = "x\nOSREPORT: NL_SAVE_BACKUP ok wii-RMGP sd:/newlauncher/saves/wii-RMGP/20261126-101507.zip\n"
        self.assertEqual(parse_backup_line(log),
                         ("wii-RMGP", "sd:/newlauncher/saves/wii-RMGP/20261126-101507.zip"))

    def test_none_without_ok(self):
        self.assertIsNone(parse_backup_line("NL_SAVE_BACKUP fail wii-RMGP \n"))


class VerifyZipTest(unittest.TestCase):
    def test_ok(self):
        self.assertEqual(verify_zip(make_zip({"smoke.sav": BYTES_A}), "wii-RMGP", {"smoke.sav": BYTES_A}), [])

    def test_wrong_bytes(self):
        problems = verify_zip(make_zip({"smoke.sav": BYTES_B}), "wii-RMGP", {"smoke.sav": BYTES_A})
        self.assertTrue(problems)

    def test_wrong_hash(self):
        problems = verify_zip(make_zip({"smoke.sav": BYTES_A}, hash_="0" * 64), "wii-RMGP", {"smoke.sav": BYTES_A})
        self.assertTrue(any("hash" in p for p in problems))

    def test_meta_must_be_first(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("smoke.sav", BYTES_A)
        self.assertTrue(verify_zip(buf.getvalue(), "wii-RMGP", {"smoke.sav": BYTES_A}))

    def test_garbage(self):
        self.assertTrue(verify_zip(b"kein zip", "wii-RMGP", {}))


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.user = Path(self.tmp.name)
        self.sd = self.user / "SDSync"
        (self.sd / "newlauncher").mkdir(parents=True)
        (self.sd / "newlauncher/emulators.ini").write_text("[gb]\ndol = sd:/apps/argv_echo/boot.dol\n")

    def tearDown(self):
        self.tmp.cleanup()

    def _store(self, name: str, unit: str = "wii-RMGP", files: dict | None = None):
        state = self.user / "save_smoke"
        state.mkdir(exist_ok=True)
        (state / f"{name}.zip").write_bytes(make_zip(files or {"smoke.sav": BYTES_A}, unit))
        (state / f"{name}.ini").write_text(f"unit = {unit}\nname = 20261126-101507.zip\n")

    def test_backup_gb_sets_save_dir_and_bytes(self):
        fx = fixture("gb", self.user)
        (self.sd / "newlauncher/saves/old").mkdir(parents=True)
        plan = prepare(fx, "backup", self.user)
        self.assertIn("save_dir = sd:/saves_smoke/gb", (self.sd / "newlauncher/emulators.ini").read_text())
        self.assertEqual((fx.host_dir / fx.file_name).read_bytes(), BYTES_A)
        self.assertFalse((self.sd / "newlauncher/saves").exists())
        self.assertEqual(plan.debug_lines, ["save_backup = gb/Tetris (World) (Rev 1).gb"])
        self.assertEqual(plan.markers, ["NL_SAVE_BACKUP ok"])

    def test_emunand_switches_save_to_emu(self):
        plan = prepare(fixture("emunand", self.user), "backup", self.user)
        self.assertEqual(plan.config_append, "[wii]\nsave = emu\n")
        self.assertTrue(plan.emunand)

    def test_restore_needs_stored_backup(self):
        with self.assertRaises(FileNotFoundError):
            prepare(fixture("nand", self.user), "restore", self.user)

    def test_restore_places_zip_and_writes_b(self):
        self._store("nand")
        fx = fixture("nand", self.user)
        (self.sd / "newlauncher/saves/wii-RMGP").mkdir(parents=True)
        (self.sd / "newlauncher/saves/wii-RMGP/29991231-000000.zip").write_bytes(b"neuer")
        plan = prepare(fx, "restore", self.user)
        self.assertEqual((fx.host_dir / fx.file_name).read_bytes(), BYTES_B)
        self.assertEqual(sorted(p.name for p in (self.sd / "newlauncher/saves/wii-RMGP").iterdir()),
                         ["20261126-101507.zip"])
        self.assertEqual(plan.debug_lines, ["save_restore = RMGP01"])
        self.assertEqual(plan.markers, ["NL_SAVE_RESTORE ok"])

    def test_recover_writes_journal_and_extra(self):
        self._store("nand")
        fx = fixture("nand", self.user)
        plan = prepare(fx, "recover", self.user)
        self.assertEqual((fx.host_dir / EXTRA_NAME).read_bytes(), BYTES_B)
        journal = (self.sd / "newlauncher/saves/restore.ini").read_text()
        self.assertIn("dir = nand:/title/00010000/524d4750/data\n", journal)
        self.assertIn("backup = sd:/newlauncher/saves/wii-RMGP/20261126-101507.zip\n", journal)
        self.assertEqual(plan.debug_lines, [])
        self.assertEqual(plan.markers, ["NL_SAVE_RECOVER rollback"])

    def test_cleanup_removes_fixture_files(self):
        self._store("nand")
        fx = fixture("nand", self.user)
        prepare(fx, "recover", self.user)
        cleanup(fx, self.user)
        self.assertFalse((fx.host_dir / fx.file_name).exists())
        self.assertFalse((fx.host_dir / EXTRA_NAME).exists())
        self.assertFalse((self.sd / "newlauncher/saves").exists())
        self.assertTrue((self.user / "save_smoke/nand.zip").is_file())  # bleibt für spätere Schritte


class PostCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.user = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_backup_stores_zip_for_later_steps(self):
        fx = fixture("gb", self.user)
        zip_bytes = make_zip({fx.file_name: BYTES_A}, "gb-tetris-world-rev-1-12345678")
        sd = {"newlauncher/saves/gb-tetris-world-rev-1-12345678/20261126-101507.zip": zip_bytes}
        log = ("NL_SAVE_BACKUP ok gb-tetris-world-rev-1-12345678 "
               "sd:/newlauncher/saves/gb-tetris-world-rev-1-12345678/20261126-101507.zip\n")
        self.assertEqual(post_check(fx, "backup", self.user, log, sd.__getitem__), [])
        self.assertEqual((self.user / "save_smoke/gb.zip").read_bytes(), zip_bytes)
        self.assertIn("unit = gb-tetris-world-rev-1-12345678", (self.user / "save_smoke/gb.ini").read_text())

    def test_backup_missing_zip_in_image(self):
        fx = fixture("gb", self.user)

        def missing(rel):
            raise FileNotFoundError(rel)
        log = "NL_SAVE_BACKUP ok u sd:/newlauncher/saves/u/x.zip\n"
        self.assertTrue(post_check(fx, "backup", self.user, log, missing))

    def test_restore_reads_sd_image(self):
        fx = fixture("gb", self.user)
        rel = f"saves_smoke/gb/{fx.file_name}"
        self.assertEqual(post_check(fx, "restore", self.user, "", {rel: BYTES_A}.__getitem__), [])
        self.assertTrue(post_check(fx, "restore", self.user, "", {rel: BYTES_B}.__getitem__))

    def test_recover_nand_reads_host_folder(self):
        fx = fixture("nand", self.user)
        fx.host_dir.mkdir(parents=True)
        (fx.host_dir / fx.file_name).write_bytes(BYTES_A)
        self.assertEqual(post_check(fx, "recover", self.user, "", {}.__getitem__), [])
        (fx.host_dir / EXTRA_NAME).write_bytes(BYTES_B)
        self.assertTrue(post_check(fx, "recover", self.user, "", {}.__getitem__))

    def test_recover_sd_extra_must_be_gone(self):
        fx = fixture("emunand", self.user)
        rel = f"{fx.sd_rel}/{fx.file_name}"
        sd = {rel: BYTES_A, f"{fx.sd_rel}/{EXTRA_NAME}": BYTES_B}
        self.assertTrue(post_check(fx, "recover", self.user, "", sd.__getitem__))


if __name__ == "__main__":
    unittest.main()
