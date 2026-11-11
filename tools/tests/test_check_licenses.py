"""Tests für tools/check_licenses.py (M11 Task 3, Spec v2 3.4)."""

import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_licenses  # noqa: E402

TABLE = """# Drittanbieter

| Name | Version | Lizenz | Quelle | verteilt als |
|---|---|---|---|---|
| FreeType | 2.14.1 | FTL | https://freetype.org | gelinkt |
| libpng | 1.6.55 | libpng | https://libpng.org | gelinkt |
| zlib | 1.3.1 | Zlib | https://zlib.net | gelinkt |
"""


class CheckLicensesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.build = self.root / "build"
        link = self.build / "CMakeFiles/newlauncher.dir/link.txt"
        link.parent.mkdir(parents=True)
        link.write_text("g++ -o x.elf a.o -lfreetype -lpng16 -lz -lm -lfoo libnl_core.a\n", encoding="utf-8")
        (self.root / "third_party").mkdir()
        (self.root / "THIRD_PARTY.md").write_text(TABLE, encoding="utf-8")

    def tearDown(self):
        self._tmp.cleanup()

    def run_check(self) -> tuple[int, str]:
        out = io.StringIO()
        with redirect_stdout(out):
            rc = check_licenses.main([str(self.build), "--root", str(self.root)])
        return rc, out.getvalue()

    def test_missing_library_fails_and_names_it(self):
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertIn("foo", out)
        self.assertNotIn("png16", out)  # Alias libpng
        self.assertNotIn("-lm", out)    # Systembibliothek

    def test_all_listed_passes(self):
        with (self.root / "THIRD_PARTY.md").open("a", encoding="utf-8") as f:
            f.write("| foo | 1.0 | MIT | https://example.org | gelinkt |\n")
        rc, _ = self.run_check()
        self.assertEqual(rc, 0)

    def test_third_party_folder_without_entry_fails(self):
        with (self.root / "THIRD_PARTY.md").open("a", encoding="utf-8") as f:
            f.write("| foo | 1.0 | MIT | https://example.org | gelinkt |\n")
        (self.root / "third_party/bar").mkdir()
        rc, out = self.run_check()
        self.assertEqual(rc, 1)
        self.assertIn("bar", out)

    def test_doctest_folder_is_test_only(self):
        with (self.root / "THIRD_PARTY.md").open("a", encoding="utf-8") as f:
            f.write("| foo | 1.0 | MIT | https://example.org | gelinkt |\n")
        (self.root / "third_party/doctest").mkdir()
        rc, _ = self.run_check()
        self.assertEqual(rc, 0)

    def test_missing_link_files_fail(self):
        empty = self.root / "leer"
        empty.mkdir()
        out = io.StringIO()
        with redirect_stdout(out):
            rc = check_licenses.main([str(empty), "--root", str(self.root)])
        self.assertEqual(rc, 1)

    def test_repo_table_covers_current_wii_build(self):
        names = check_licenses.table_names(Path(check_licenses.REPO_ROOT / "THIRD_PARTY.md").read_text(encoding="utf-8"))
        for lib in ("freetype", "png16", "z", "bz2", "brotlidec", "brotlicommon", "ogg", "vorbisidec", "fat",
                    "ogc", "asnd", "bte", "wiiuse"):
            self.assertTrue(check_licenses.is_listed(check_licenses.component_for_lib(lib), names), lib)


if __name__ == "__main__":
    unittest.main()
