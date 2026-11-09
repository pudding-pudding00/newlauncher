import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from devkitpro import DEVKITPRO_BASH, wii_build_invocation  # noqa: E402


class DevkitProTest(unittest.TestCase):
    def test_windows_invocation_uses_devkitpro_bash_in_repo(self):
        cmd, env = wii_build_invocation(Path(r"C:\x\repo"), "win32")
        self.assertEqual(cmd[0], DEVKITPRO_BASH)
        self.assertEqual(cmd[1], "-lc")
        self.assertIn("cmake --preset wii && cmake --build --preset wii", cmd[2])
        self.assertEqual(env, {"CHERE_INVOKING": "1"})

    def test_windows_invocation_never_embeds_repo_path(self):
        # Pfade mit Leerzeichen bleiben sicher, weil der Ordner über cwd + CHERE_INVOKING
        # kommt und nie in den Shell-String eingesetzt wird.
        repo = Path(r"D:\Mein Ordner\repo")
        cmd, _ = wii_build_invocation(repo, "win32")
        for part in cmd:
            self.assertNotIn("Mein Ordner", part)

    def test_linux_invocation_runs_cmake_directly(self):
        cmd, env = wii_build_invocation(Path("/src/repo"), "linux")
        self.assertEqual(cmd, ["sh", "-c", "cmake --preset wii && cmake --build --preset wii"])
        self.assertEqual(env, {})


if __name__ == "__main__":
    unittest.main()
