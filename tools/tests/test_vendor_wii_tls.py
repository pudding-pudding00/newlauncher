"""Tests für tools/vendor_wii_tls.py (M11 Task 4, Spec v2 4.1)."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import vendor_wii_tls  # noqa: E402


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()


class VendorTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.repo = self.tmp / "wii_tls"
        for rel, text in (("include/x.h", "int x;\n"), ("tests/t.c", "t\n"), ("bench/b.c", "b\n"),
                          ("examples/e.c", "e\n"), ("docs/d.md", "d\n"), ("tools/hwtest.py", "p\n"),
                          ("tools/gen_ca_bundle.py", "g\n"), ("tools/check_ct.cmake", "# cmake\n"),
                          ("CMakeLists.txt", "project(x)\n")):
            path = self.repo / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        git(self.repo.parent, "init", "-q", str(self.repo))
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "add", ".")
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "-m", "init")
        git(self.repo, "remote", "add", "origin", "https://user:secret@example.invalid/wii_tls.git")
        self.sha = git(self.repo, "rev-parse", "HEAD")
        self.dest = self.tmp / "out/wii_tls"

    def tearDown(self):
        self._tmp.cleanup()

    def test_vendors_without_tests_bench_examples_docs(self):
        self.dest.mkdir(parents=True)
        (self.dest / "stale.txt").write_text("alt", encoding="utf-8")
        rc = vendor_wii_tls.main([str(self.repo), "--rev", "HEAD", "--dest", str(self.dest)])
        self.assertEqual(rc, 0)
        self.assertTrue((self.dest / "include/x.h").is_file())
        self.assertTrue((self.dest / "CMakeLists.txt").is_file())
        for gone in ("tests", "bench", "examples", "docs", "tools/hwtest.py", "stale.txt"):
            self.assertFalse((self.dest / gone).exists(), gone)
        self.assertTrue((self.dest / "tools/gen_ca_bundle.py").is_file())
        self.assertTrue((self.dest / "tools/check_ct.cmake").is_file())
        info = (self.dest / "VENDORED.txt").read_text(encoding="utf-8")
        self.assertIn(f"commit={self.sha}", info)
        self.assertIn("date=", info)
        self.assertIn("source=https://example.invalid/wii_tls.git", info)
        self.assertNotIn("secret", info)

    def test_dirty_repo_fails(self):
        (self.repo / "include/x.h").write_text("int y;\n", encoding="utf-8")
        rc = vendor_wii_tls.main([str(self.repo), "--dest", str(self.dest)])
        self.assertEqual(rc, 1)
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
