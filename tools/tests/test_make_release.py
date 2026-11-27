"""Tests für tools/make_release.py und tools/notices.py (M11 Task 3, Spec v2 3.4)."""

import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_release  # noqa: E402
import notices  # noqa: E402


class MakeReleaseTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "LICENSE").write_text("GNU GENERAL PUBLIC LICENSE\n", encoding="utf-8")
        (self.root / "THIRD_PARTY.md").write_text("# Drittanbieter\n", encoding="utf-8")
        (self.root / "LICENSES").mkdir()
        (self.root / "LICENSES/Apache-2.0.txt").write_text("Apache License\nVersion 2.0\n", encoding="utf-8")
        (self.root / "LICENSES/MIT.txt").write_text("MIT License\n", encoding="utf-8")
        app = self.root / "dist/apps/newlauncher"
        app.mkdir(parents=True)
        (app / "meta.xml").write_text("<app/>", encoding="utf-8")
        (app / "icon.png").write_bytes(b"\x89PNG")
        self.dol = self.root / "newlauncher.dol"
        self.dol.write_bytes(b"DOL")

    def tearDown(self):
        self._tmp.cleanup()

    def build(self) -> Path:
        out = self.root / "out/newlauncher-1.0.zip"
        rc = make_release.main(["--out", str(out), "--dol", str(self.dol), "--root", str(self.root)])
        self.assertEqual(rc, 0)
        return out

    def test_update_ini_describes_zip(self):
        (self.root / "CMakeLists.txt").write_text("project(newlauncher VERSION 1.2.3 LANGUAGES C CXX)\n",
                                                  encoding="utf-8")
        out = self.build()
        ini = (out.parent / "update.ini").read_text(encoding="utf-8")
        with zipfile.ZipFile(out) as z:
            unzipped = sum(i.file_size for i in z.infolist())
        self.assertIn("[update]\n", ini)
        self.assertIn("version = 1.2.3\n", ini)
        self.assertIn("zip = newlauncher-1.0.zip\n", ini)
        self.assertIn(f"zip_size = {out.stat().st_size}\n", ini)
        self.assertIn(f"unzipped_size = {unzipped}\n", ini)

    def test_version_option_overrides_cmake(self):
        out = self.root / "out/nl.zip"
        rc = make_release.main(["--out", str(out), "--dol", str(self.dol), "--root", str(self.root),
                                "--version", "9.8.7"])
        self.assertEqual(rc, 0)
        self.assertIn("version = 9.8.7\n", (out.parent / "update.ini").read_text(encoding="utf-8"))

    def test_zip_has_exactly_the_five_files(self):
        with zipfile.ZipFile(self.build()) as z:
            self.assertEqual(sorted(z.namelist()), sorted(
                f"apps/newlauncher/{n}" for n in ("boot.dol", "meta.xml", "icon.png", "LICENSE.txt", "THIRD_PARTY.txt")))
            self.assertEqual(z.read("apps/newlauncher/boot.dol"), b"DOL")
            self.assertEqual(z.read("apps/newlauncher/LICENSE.txt"), b"GNU GENERAL PUBLIC LICENSE\n")

    def test_third_party_txt_appends_license_texts(self):
        with zipfile.ZipFile(self.build()) as z:
            text = z.read("apps/newlauncher/THIRD_PARTY.txt").decode("utf-8")
        self.assertTrue(text.startswith("# Drittanbieter"))
        self.assertIn("Apache License", text)
        self.assertIn("MIT License", text)
        self.assertLess(text.index("Apache-2.0.txt"), text.index("MIT.txt"))  # feste Reihenfolge

    def test_missing_dol_fails(self):
        self.dol.unlink()
        rc = make_release.main(["--out", str(self.root / "x.zip"), "--dol", str(self.dol), "--root", str(self.root)])
        self.assertEqual(rc, 1)

    def test_meta_xml_carries_the_release_version(self):
        (self.root / "dist/apps/newlauncher/meta.xml").write_text(
            '<app version="1">\n  <version>0.0.1</version>\n</app>\n', encoding="utf-8")
        out = self.root / "out/nl.zip"
        rc = make_release.main(["--out", str(out), "--dol", str(self.dol), "--root", str(self.root),
                                "--version", "0.0.3"])
        self.assertEqual(rc, 0)
        with zipfile.ZipFile(out) as z:
            meta = z.read("apps/newlauncher/meta.xml").decode("utf-8")
        self.assertIn("<version>0.0.3</version>", meta)
        self.assertNotIn("<version>0.0.1</version>", meta)

    def test_meta_with_version_only_replaces_the_version_element(self):
        self.assertEqual(make_release.meta_with_version("<app><version>1.0</version></app>", "2.0"),
                         "<app><version>2.0</version></app>")
        self.assertEqual(make_release.meta_with_version("<app/>", "2.0"), "<app/>")

    def test_update_ini_can_point_at_an_absolute_zip_url(self):
        out = self.root / "out/newlauncher-0.0.3.zip"
        rc = make_release.main(["--out", str(out), "--dol", str(self.dol), "--root", str(self.root),
                                "--version", "0.0.3", "--zip-base-url", "https://example.invalid/dl/dev-0.0.3"])
        self.assertEqual(rc, 0)
        ini = (out.parent / "update.ini").read_text(encoding="utf-8")
        self.assertIn("zip = https://example.invalid/dl/dev-0.0.3/newlauncher-0.0.3.zip\n", ini)

    def test_zip_base_url_must_be_https(self):
        rc = make_release.main(["--out", str(self.root / "x.zip"), "--dol", str(self.dol), "--root", str(self.root),
                                "--zip-base-url", "http://example.invalid/dl"])
        self.assertEqual(rc, 1)

    def test_repo_notices_contain_apache(self):
        self.assertIn("Apache License", notices.third_party_text(notices.REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
