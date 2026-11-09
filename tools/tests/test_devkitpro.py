import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from devkitpro import DEVKITPRO_BASH, to_msys_path, wii_build_invocation  # noqa: E402


class DevkitProTest(unittest.TestCase):
    def test_to_msys_path_converts_drive(self):
        self.assertEqual(to_msys_path(r"<repo-root>"),
                         "/path/to/newlauncher")

    def test_to_msys_path_keeps_spaces(self):
        self.assertEqual(to_msys_path(r"D:\Mein Ordner\repo"), "/d/Mein Ordner/repo")

    def test_windows_invocation_uses_devkitpro_bash_in_repo(self):
        cmd, env = wii_build_invocation(Path(r"C:\x\repo"), "win32")
        self.assertEqual(cmd[0], DEVKITPRO_BASH)
        self.assertEqual(cmd[1], "-lc")
        self.assertIn("cmake --preset wii && cmake --build --preset wii", cmd[2])
        self.assertEqual(env, {"CHERE_INVOKING": "1"})

    def test_linux_invocation_runs_cmake_directly(self):
        cmd, env = wii_build_invocation(Path("/src/repo"), "linux")
        self.assertEqual(cmd, ["sh", "-c", "cmake --preset wii && cmake --build --preset wii"])
        self.assertEqual(env, {})


if __name__ == "__main__":
    unittest.main()
