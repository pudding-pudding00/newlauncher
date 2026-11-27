"""Tests for tools/dev_release.py: version numbers of the development releases published on merges into main."""

import contextlib
import io
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import dev_release  # noqa: E402


class NextDevVersionTest(unittest.TestCase):
    def test_first_release_is_0_0_1(self):
        self.assertEqual(dev_release.next_dev_version([]), "0.0.1")

    def test_counts_up_from_the_newest_dev_tag(self):
        self.assertEqual(dev_release.next_dev_version(["dev-0.0.1", "dev-0.0.2"]), "0.0.3")

    def test_compares_numbers_not_text(self):
        self.assertEqual(dev_release.next_dev_version(["dev-0.0.9", "dev-0.0.10", "dev-0.0.2"]), "0.0.11")

    def test_ignores_tags_that_are_not_dev_releases(self):
        tags = ["v1.0.0", "v0.0.7", "dev-0.0.1-rc1", "dev-0.1.0", "dev-0.0.2"]
        self.assertEqual(dev_release.next_dev_version(tags), "0.0.3")


class ResolveVersionTest(unittest.TestCase):
    def test_commit_that_already_has_a_dev_tag_keeps_its_number(self):
        # A re-run of the publish job for the same merge must not publish it again under a new number.
        self.assertEqual(dev_release.resolve_version(["dev-0.0.1", "dev-0.0.2"], ["dev-0.0.2"]), "0.0.2")

    def test_untagged_commit_gets_the_next_number(self):
        self.assertEqual(dev_release.resolve_version(["dev-0.0.1"], []), "0.0.2")


class RepositoryTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "user.name", "test")
        self.git("commit", "-q", "--allow-empty", "-m", "first")
        self.git("tag", "dev-0.0.1")
        self.git("commit", "-q", "--allow-empty", "-m", "second")
        self.git("tag", "dev-0.0.2")
        self.git("tag", "v1.0.0")
        self.git("commit", "-q", "--allow-empty", "-m", "third")

    def tearDown(self):
        self._tmp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def test_repo_dev_tags_lists_only_dev_releases(self):
        self.assertEqual(dev_release.repo_dev_tags(self.root), ["dev-0.0.1", "dev-0.0.2"])

    def test_untagged_head_gets_the_next_number(self):
        self.assertEqual(dev_release.version_for_rev(self.root, "HEAD"), "0.0.3")

    def test_head_that_is_already_tagged_keeps_its_number(self):
        self.git("tag", "dev-0.0.3")
        self.assertEqual(dev_release.version_for_rev(self.root, "HEAD"), "0.0.3")

    def test_cli_prints_the_next_version(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = dev_release.main(["next-version", "--root", str(self.root)])
        self.assertEqual(rc, 0)
        self.assertEqual(out.getvalue(), "0.0.3\n")


class PackageVersionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.zip_path = self.dir / "newlauncher-0.0.3.zip"
        with zipfile.ZipFile(self.zip_path, "w") as z:
            z.writestr("apps/newlauncher/meta.xml", '<app version="1">\n  <version>0.0.3</version>\n</app>\n')
            z.writestr("apps/newlauncher/boot.dol", b"DOL")

    def tearDown(self):
        self._tmp.cleanup()

    def test_package_version_reads_meta_xml(self):
        self.assertEqual(dev_release.package_version(self.zip_path), "0.0.3")

    def test_package_without_meta_xml_has_no_version(self):
        bare = self.dir / "bare.zip"
        with zipfile.ZipFile(bare, "w") as z:
            z.writestr("apps/newlauncher/boot.dol", b"DOL")
        self.assertIsNone(dev_release.package_version(bare))

    def test_cli_check_package_accepts_the_matching_version(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = dev_release.main(["check-package", str(self.zip_path), "0.0.3"])
        self.assertEqual(rc, 0)

    def test_cli_check_package_rejects_another_version(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = dev_release.main(["check-package", str(self.zip_path), "0.0.4"])
        self.assertEqual(rc, 1)
        self.assertIn("'0.0.3'", err.getvalue())

    def test_cli_check_package_rejects_a_zip_above_the_size_limit(self):
        err = io.StringIO()
        saved = dev_release.MAX_ZIP_BYTES
        dev_release.MAX_ZIP_BYTES = 16  # the test ZIP is larger than this
        try:
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
                rc = dev_release.main(["check-package", str(self.zip_path), "0.0.3"])
        finally:
            dev_release.MAX_ZIP_BYTES = saved
        self.assertEqual(rc, 1)
        self.assertIn("accepts at most 16", err.getvalue())

    def test_size_limit_matches_the_launcher(self):
        # The launcher drops an update.ini whose ZIP is larger than kMaxUpdateZipBytes (src/core/self_update.hpp).
        header = (dev_release.REPO_ROOT / "src/core/self_update.hpp").read_text(encoding="utf-8")
        expr = re.search(r"kMaxUpdateZipBytes\s*=\s*([^;]+);", header).group(1)
        launcher_limit = 1
        for factor in re.findall(r"\d+", expr):
            launcher_limit *= int(factor)
        self.assertEqual(dev_release.MAX_ZIP_BYTES, launcher_limit)


if __name__ == "__main__":
    unittest.main()
