"""Tests für tools/mbedtls_src.py: Mbed-TLS-Quelle für den Windows-Build ohne Symlinks entpacken."""

import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import mbedtls_src  # noqa: E402


def make_archive(path: Path) -> str:
    with tarfile.open(path, "w:bz2") as tf:
        for name, data in (("mbedtls-4.1.1/CMakeLists.txt", b"project(m)\n"), ("mbedtls-4.1.1/x/y.c", b"int y;\n")):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
        link = tarfile.TarInfo("mbedtls-4.1.1/examples/link.c")
        link.type = tarfile.SYMTYPE
        link.linkname = "../x/y.c"
        tf.addfile(link)
    return hashlib.sha256(path.read_bytes()).hexdigest()


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.archive = self.tmp / "m.tar.bz2"
        self.sha = make_archive(self.archive)

    def tearDown(self):
        self._tmp.cleanup()

    def test_extracts_files_and_skips_symlinks(self):
        dest = self.tmp / "src"
        self.assertTrue(mbedtls_src.extract_without_symlinks(self.archive, self.sha, dest))
        self.assertEqual((dest / "x/y.c").read_bytes(), b"int y;\n")
        self.assertTrue((dest / "CMakeLists.txt").is_file())
        self.assertFalse((dest / "examples/link.c").exists())

    def test_wrong_hash_extracts_nothing(self):
        dest = self.tmp / "src"
        self.assertFalse(mbedtls_src.extract_without_symlinks(self.archive, "0" * 64, dest))
        self.assertFalse(dest.exists())

    def test_constants_match_vendored_wii_tls(self):
        # Gleiche Quelle wie FetchContent in wii_tls, sonst baut Windows eine andere Version als die CI.
        cmake = (mbedtls_src.REPO_ROOT / "third_party/wii_tls/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(mbedtls_src.URL, cmake)
        self.assertIn(f"SHA256={mbedtls_src.SHA256}", cmake)


if __name__ == "__main__":
    unittest.main()
