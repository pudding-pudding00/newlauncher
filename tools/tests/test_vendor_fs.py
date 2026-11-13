"""Tests für tools/vendor_fs.py (M16 Task 2, Spec v2 13)."""

import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import vendor_fs  # noqa: E402


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()


PATCH = """--- a/source/a.c
+++ b/source/a.c
@@ -1 +1,2 @@
 int a;
+int patched;
"""


class VendorFsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.repo = self.tmp / "lib"
        for rel, text in (("source/a.c", "int a;\n"), ("source/skip.c", "s\n"), ("include/x.h", "int x;\n"),
                          ("LICENSE", "GPL\n"), ("example/e.c", "e\n")):
            path = self.repo / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
        git(self.repo.parent, "init", "-q", str(self.repo))
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "add", ".")
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-q", "-m", "init")
        self.sha = git(self.repo, "rev-parse", "HEAD")
        self.dest = self.tmp / "out/lib"
        (self.dest / "patches").mkdir(parents=True)
        (self.dest / "patches/0001-test.patch").write_text(PATCH, encoding="utf-8", newline="\n")
        self.comp = vendor_fs.Component(rev=self.sha, source="https://example.invalid/lib.git",
                                        license="GPL-2.0-or-later", keep=("source", "include", "LICENSE"),
                                        exclude=("source/skip.c",))
        vendor_fs.COMPONENTS["testlib"] = self.comp

    def tearDown(self):
        vendor_fs.COMPONENTS.pop("testlib", None)
        self._tmp.cleanup()

    def run_main(self, *extra: str) -> tuple[int, str]:
        out = io.StringIO()
        with redirect_stdout(out):
            rc = vendor_fs.main(["testlib", str(self.repo), "--dest", str(self.dest), *extra])
        return rc, out.getvalue()

    def test_record_then_vendor_applies_patches_and_writes_vendored(self):
        rc, _ = self.run_main("--record")
        self.assertEqual(rc, 0)
        manifest = (self.dest / "UPSTREAM.sha256").read_text(encoding="utf-8")
        self.assertIn("source/a.c", manifest)
        self.assertNotIn("skip.c", manifest)
        rc, out = self.run_main()
        self.assertEqual(rc, 0, out)
        up = self.dest / "upstream"
        self.assertIn("int patched;", (up / "source/a.c").read_text(encoding="utf-8"))
        self.assertTrue((up / "include/x.h").is_file())
        self.assertFalse((up / "source/skip.c").exists())
        self.assertFalse((up / "example").exists())
        info = (self.dest / "VENDORED.txt").read_text(encoding="utf-8")
        self.assertIn(f"commit={self.sha}", info)
        self.assertIn("license=GPL-2.0-or-later", info)
        self.assertIn("patches=0001-test.patch", info)
        self.assertIn("source=https://example.invalid/lib.git", info)

    def test_wrong_hash_aborts_with_filename(self):
        self.assertEqual(self.run_main("--record")[0], 0)
        m = self.dest / "UPSTREAM.sha256"
        lines = m.read_text(encoding="utf-8").splitlines()
        lines = [("0" * 64 + l[64:]) if l.endswith("include/x.h") else l for l in lines]
        m.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        rc, out = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("include/x.h", out)
        self.assertFalse((self.dest / "upstream").exists())
        self.assertFalse((self.dest / "VENDORED.txt").exists())

    def test_missing_manifest_aborts(self):
        rc, out = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("UPSTREAM.sha256", out)

    def test_rev_mismatch_with_component_aborts(self):
        self.comp.rev = "0" * 40
        rc, out = self.run_main("--record")
        self.assertEqual(rc, 1)
        self.assertIn("VENDOR FEHLER", out)


if __name__ == "__main__":
    unittest.main()
