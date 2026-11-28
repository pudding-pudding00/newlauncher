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
        self.assertTrue(cmd[2].startswith("cmake --preset wii -DNL_TLS=ON"), cmd[2])
        self.assertTrue(cmd[2].endswith(" && cmake --build --preset wii --parallel $(nproc)"), cmd[2])
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
        self.assertEqual(cmd, ["sh", "-c", "cmake --preset wii -DNL_TLS=ON -DNL_FS_NTFS=ON -DNL_DOLPHIN_DEV_TLS=OFF && cmake --build --preset wii --parallel $(nproc)"])
        self.assertEqual(env, {})

    def test_dolphin_dev_tls_builds_in_own_directory(self):
        cmd, _ = wii_build_invocation(Path("/src/repo"), "linux", dolphin_dev_tls=True)
        self.assertIn("-B build/wii-dolphin", cmd[2])
        self.assertIn("-DNL_DOLPHIN_DEV_TLS=ON", cmd[2])

    def test_no_tls_switches_option_off(self):
        # Der Wert steht immer explizit im Befehl: CMake merkt sich Optionen im Cache, ein Build
        # ohne -DNL_TLS=ON nach einem --no-tls-Build bliebe sonst stillschweigend ohne HTTPS.
        cmd, _ = wii_build_invocation(Path("/src/repo"), "linux", tls=False)
        self.assertIn("-DNL_TLS=OFF", cmd[2])
        self.assertNotIn("-DNL_TLS=ON", cmd[2])

    def test_no_tls_builds_in_own_directory(self):
        # Eigener Ordner je Variante: ein Wechsel ON/OFF im selben Ordner baut sonst fast alles neu (~1 min).
        cmd, _ = wii_build_invocation(Path("/src/repo"), "linux", tls=False)
        self.assertEqual(cmd[2], "cmake --preset wii -B build/wii-notls -DNL_TLS=OFF -DNL_FS_NTFS=OFF -DNL_DOLPHIN_DEV_TLS=OFF && "
                                 "cmake --build build/wii-notls --parallel $(nproc)")

    def test_ntfs_follows_tls_variant(self):
        # M16: CI baut ON/ON und OFF/OFF; der Schalter steht explizit im Befehl (CMake-Cache).
        cmd, _ = wii_build_invocation(Path("/src/repo"), "linux")
        self.assertIn("-DNL_FS_NTFS=ON", cmd[2])
        cmd, _ = wii_build_invocation(Path(r"C:\x\repo"), "win32")
        self.assertIn("-DNL_FS_NTFS=ON", cmd[2])
        cmd, _ = wii_build_invocation(Path("/src/repo"), "linux", tls=False)
        self.assertIn("-DNL_FS_NTFS=OFF", cmd[2])

    def test_windows_tls_uses_local_mbedtls_source(self):
        # MSYS-tar kann die Symlinks im Mbed-TLS-Archiv nicht anlegen (tools/mbedtls_src.py).
        cmd, _ = wii_build_invocation(Path(r"C:\x\repo"), "win32")
        self.assertIn("-DFETCHCONTENT_SOURCE_DIR_MBEDTLS=$(pwd -P)/build/mbedtls/mbedtls-4.1.2", cmd[2])
        cmd, _ = wii_build_invocation(Path(r"C:\x\repo"), "win32", tls=False)
        self.assertNotIn("FETCHCONTENT", cmd[2])


if __name__ == "__main__":
    unittest.main()
