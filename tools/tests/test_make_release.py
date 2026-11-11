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

    def test_repo_notices_contain_apache(self):
        self.assertIn("Apache License", notices.third_party_text(notices.REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
