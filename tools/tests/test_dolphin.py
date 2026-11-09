import configparser
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from deploy_dolphin import deploy  # noqa: E402
from dolphin import (dolphin_command, log_path, reset_log, wait_for_marker,  # noqa: E402
                     write_dev_config)
from dolphin_smoke import main as smoke_main  # noqa: E402


def read_ini(path: Path) -> configparser.ConfigParser:
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(path, encoding="utf-8")
    return ini


class DolphinToolsTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.user = self.tmp / "dolphin-user"
        self.log = self.tmp / "dolphin.log"
        self.dol = self.tmp / "newlauncher.dol"
        self.dol.write_bytes(b"\0" * 16)

    def tearDown(self):
        self._tmp.cleanup()

    def test_write_dev_config_sets_sd_sync_and_logger(self):
        write_dev_config(self.user)
        d = read_ini(self.user / "Config/Dolphin.ini")
        l = read_ini(self.user / "Config/Logger.ini")
        self.assertEqual(d["Core"]["WiiSDCardEnableFolderSync"], "True")
        self.assertEqual(d["General"]["WiiSDCardSyncFolder"], str(self.user / "SDSync"))
        self.assertEqual(d["Analytics"]["PermissionAsked"], "True")
        self.assertEqual(l["Options"]["WriteToFile"], "True")
        self.assertEqual(l["Logs"]["OSREPORT"], "True")

    def test_dolphin_command_always_isolates_user_dir(self):
        cmd = dolphin_command(Path("D.exe"), self.user, Path("a.dol"))
        self.assertEqual(cmd, ["D.exe", "-u", str(self.user), "-b", "-e", "a.dol"])

    def test_reset_log_removes_stale_marker(self):
        log_path(self.user).parent.mkdir(parents=True)
        log_path(self.user).write_text("NL_READY\n")
        reset_log(self.user)
        self.assertFalse(log_path(self.user).exists())

    def test_wait_for_marker_finds_marker(self):
        self.log.write_text("OSREPORT: newlauncher 0.0.1\nOSREPORT: NL_READY\n")
        self.assertTrue(wait_for_marker(self.log, "NL_READY", 2.0, lambda: True))

    def test_wait_for_marker_times_out(self):
        self.log.write_text("OSREPORT: newlauncher 0.0.1\n")
        t = time.monotonic()
        self.assertFalse(wait_for_marker(self.log, "NL_READY", 0.5, lambda: True))
        self.assertLess(time.monotonic() - t, 2.0)

    def test_wait_for_marker_stops_when_process_dies(self):
        t = time.monotonic()
        self.assertFalse(wait_for_marker(self.log, "NL_READY", 30.0, lambda: False))
        self.assertLess(time.monotonic() - t, 1.0)

    def test_deploy_places_boot_dol_and_meta(self):
        dist = self.tmp / "dist"
        (dist / "apps/newlauncher").mkdir(parents=True)
        (dist / "apps/newlauncher/meta.xml").write_text("<app/>")
        sd = self.tmp / "sd"
        deploy(self.dol, sd, dist)
        self.assertTrue((sd / "apps/newlauncher/boot.dol").is_file())
        self.assertTrue((sd / "apps/newlauncher/meta.xml").is_file())

    def test_smoke_reports_missing_dolphin(self):
        self.assertEqual(smoke_main(["--dolphin", str(self.tmp / "fehlt.exe"), "--dol", str(self.dol),
                                     "--user-dir", str(self.user)]), 2)


if __name__ == "__main__":
    unittest.main()
