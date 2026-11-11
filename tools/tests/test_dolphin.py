import configparser
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from deploy_dolphin import FIXTURES_DIR, deploy  # noqa: E402
from deploy_dolphin import main as deploy_main  # noqa: E402
from dolphin import (dolphin_command, log_path, reset_log, wait_for_markers,  # noqa: E402
                     write_dev_config)
from dolphin_smoke import main as smoke_main  # noqa: E402


def process_alive(pid: int) -> bool:
    if sys.platform == "win32":
        out = subprocess.check_output(["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                                      stdin=subprocess.DEVNULL)
        return str(pid).encode() in out
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


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
        self.assertEqual(cmd, ["D.exe", "-u", str(self.user), "-b", "-C", "Dolphin.DSP.Volume=0", "-e", "a.dol"])

    def test_reset_log_removes_stale_marker(self):
        log_path(self.user).parent.mkdir(parents=True)
        log_path(self.user).write_text("NL_READY\n")
        reset_log(self.user)
        self.assertFalse(log_path(self.user).exists())

    def test_wait_for_markers_finds_marker(self):
        self.log.write_text("OSREPORT: newlauncher 0.0.1\nOSREPORT: NL_READY\n")
        self.assertEqual(wait_for_markers(self.log, ["NL_READY"], 2.0, lambda: True), [])

    def test_wait_for_markers_times_out(self):
        self.log.write_text("OSREPORT: newlauncher 0.0.1\n")
        t = time.monotonic()
        self.assertEqual(wait_for_markers(self.log, ["NL_READY"], 0.5, lambda: True), ["NL_READY"])
        self.assertLess(time.monotonic() - t, 2.0)

    def test_wait_for_markers_stops_when_process_dies(self):
        t = time.monotonic()
        self.assertEqual(wait_for_markers(self.log, ["NL_READY"], 30.0, lambda: False), ["NL_READY"])
        self.assertLess(time.monotonic() - t, 1.0)

    def test_wait_for_markers_returns_missing(self):
        self.log.write_text("OSREPORT: NL_READY\n")
        self.assertEqual(wait_for_markers(self.log, ["NL_READY", "NL_SELFTEST_OK"], 0.5, lambda: True),
                         ["NL_SELFTEST_OK"])

    def test_wait_for_markers_rereads_log_after_process_death(self):
        # Der Prozess schreibt den Marker und stirbt zwischen zwei Abfragen.
        def is_alive():
            self.log.write_text("OSREPORT: NL_READY\n")
            return False
        self.assertEqual(wait_for_markers(self.log, ["NL_READY"], 5.0, is_alive), [])

    def test_wait_for_markers_tolerates_locked_log(self):
        self.log.write_text("OSREPORT: NL_READY\n")
        with mock.patch.object(Path, "read_text", side_effect=PermissionError):
            self.assertEqual(wait_for_markers(self.log, ["NL_READY"], 0.3, lambda: True), ["NL_READY"])

    def test_deploy_main_writes_dev_config(self):
        dist = self.tmp / "dist"
        dist.mkdir()
        self.assertEqual(deploy_main(user_dir=self.user, dol=self.dol, dist_dir=dist), 0)
        self.assertTrue((self.user / "Config/Dolphin.ini").is_file())
        self.assertTrue((self.user / "SDSync/apps/newlauncher/boot.dol").is_file())

    def test_deploy_places_boot_dol_and_meta(self):
        dist = self.tmp / "dist"
        (dist / "apps/newlauncher").mkdir(parents=True)
        (dist / "apps/newlauncher/meta.xml").write_text("<app/>")
        sd = self.tmp / "sd"
        deploy(self.dol, sd, dist)
        self.assertTrue((sd / "apps/newlauncher/boot.dol").is_file())
        self.assertTrue((sd / "apps/newlauncher/meta.xml").is_file())

    def test_deploy_copies_argv_echo(self):
        dist = self.tmp / "dist"
        dist.mkdir()
        echo = self.tmp / "argv_echo.dol"
        echo.write_bytes(b"echo")
        sd = self.tmp / "sd"
        deploy(self.dol, sd, dist, argv_echo=echo)
        self.assertEqual((sd / "apps/argv_echo/boot.dol").read_bytes(), b"echo")

    def test_deploy_installs_test_channel_into_nand(self):
        dist = self.tmp / "dist"
        dist.mkdir()
        echo = self.tmp / "argv_echo.dol"
        echo.write_bytes(b"echo")
        nand = self.tmp / "Wii"
        deploy(self.dol, self.tmp / "sd", dist, argv_echo=echo, wii_root=nand)
        self.assertEqual((nand / "title/00010001/4e4c5443/content/00000001.app").read_bytes(), b"echo")
        deploy(self.dol, self.tmp / "sd2", dist, argv_echo=self.tmp / "fehlt.dol", wii_root=self.tmp / "Wii2")
        self.assertFalse((self.tmp / "Wii2").exists())

    def test_deploy_removes_stale_launch_file(self):
        # Ein abgebrochener Kanalstart hinterlässt /sys/launch.sys; Dolphin startete den Titel sonst beim nächsten Boot.
        dist = self.tmp / "dist"
        dist.mkdir()
        nand = self.tmp / "Wii"
        (nand / "sys").mkdir(parents=True)
        (nand / "sys/launch.sys").write_bytes(b"alt")
        deploy(self.dol, self.tmp / "sd", dist, argv_echo=self.tmp / "fehlt.dol", wii_root=nand)
        self.assertFalse((nand / "sys/launch.sys").exists())

    def test_smoke_installs_test_channel(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        with mock.patch("dolphin_smoke.deploy", wraps=deploy) as d:
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2"])
        self.assertEqual(d.call_args.kwargs.get("wii_root"), self.user.resolve() / "Wii")

    def test_deploy_skips_missing_argv_echo(self):
        dist = self.tmp / "dist"
        dist.mkdir()
        sd = self.tmp / "sd"
        deploy(self.dol, sd, dist, argv_echo=self.tmp / "fehlt.dol")
        self.assertFalse((sd / "apps/argv_echo").exists())

    def test_smoke_autostart_appends_debug_section(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--autostart", "nes/Zelda (Europe) (Rev 1).nes"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\nautostart = nes/Zelda (Europe) (Rev 1).nes\n"))

    def test_smoke_debug_option_appends_lines(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--debug", "perf=1", "--debug", "x = y"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\nperf = 1\nx = y\n"))

    def test_smoke_cover_server_writes_debug_keys_and_pngs(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        seen = {}

        def fake_start(root):
            root = Path(root)
            seen["en"] = (root / "EN/RMCP01.png").read_bytes()[:8]
            seen["us"] = (root / "US/GM8P01.png").read_bytes()[:8]
            seen["count"] = len([p for p in root.rglob("*") if p.is_file()])
            return mock.Mock(), 12345

        with mock.patch("dolphin_smoke.start_cover_server", side_effect=fake_start) as start:
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server"])
        start.assert_called_once()
        self.assertEqual(seen["en"], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(seen["us"], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(seen["count"], 2)
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\ncover_url = http://127.0.0.1:12345\nfetch_covers = 1\n"))

    def test_smoke_cover_server_and_autostart_share_one_debug_section(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        with mock.patch("dolphin_smoke.start_cover_server", return_value=(mock.Mock(), 7)):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server", "--autostart", "nes/a.nes"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertEqual(text.count("[debug]"), 1)
        self.assertTrue(text.endswith("[debug]\nautostart = nes/a.nes\ncover_url = http://127.0.0.1:7\n"
                                      "fetch_covers = 1\n"))

    def test_write_emu_server_files_list_matches_zip(self):
        import zipfile
        from dolphin_smoke import EMU_SERVER_TIMESTAMP, write_emu_server_files
        root = self.tmp / "srv"
        dol = self.tmp / "echo.dol"
        dol.write_bytes(b"D" * 1000)
        write_emu_server_files(root, dol)
        zpath = root / "hbb/fceugx/fceugx.zip"
        with zipfile.ZipFile(zpath) as z:
            names = sorted(z.namelist())
            unzipped = sum(i.file_size for i in z.infolist())
            self.assertEqual(z.read("apps/fceugx/boot.dol"), b"D" * 1000)
        self.assertEqual(names, ["apps/fceugx/boot.dol", "apps/fceugx/meta.xml", "fceugx/fceugx.cfg"])
        lines = (root / "hbb/listv036.txt").read_bytes().decode("utf-8").split("\r\n")  # OSC-Liste nutzt CRLF
        head = next(line for line in lines if line.startswith("fceugx "))
        f = head.split()
        self.assertEqual(f[1], str(EMU_SERVER_TIMESTAMP))
        self.assertEqual(int(f[3]), unzipped)
        self.assertEqual(int(f[5]), zpath.stat().st_size)
        self.assertEqual(f[4], "dol")
        block = lines[lines.index(head):]
        self.assertGreaterEqual(len(block), 7)

    def test_smoke_emu_server_sets_debug_and_sync_emulators_ini(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        sync_apps = self.user / "SDSync/apps/fceugx"
        sync_apps.mkdir(parents=True)
        (sync_apps / "boot.dol").write_bytes(b"alt")
        fixture_ini = FIXTURES_DIR / "newlauncher/emulators.ini"
        before = fixture_ini.read_bytes()
        seen = {}

        def fake_start(root):
            seen["zip"] = (Path(root) / "hbb/fceugx/fceugx.zip").is_file()
            return mock.Mock(), 4321

        echo = self.tmp / "argv_echo.dol"  # die CI baut im Host-Job keine Wii-DOLs
        echo.write_bytes(b"E" * 64)
        with mock.patch("dolphin_smoke.start_cover_server", side_effect=fake_start), \
                mock.patch("dolphin_smoke.ARGV_ECHO_DOL", echo):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--emu-server"])
        self.assertTrue(seen.get("zip"))
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\nemu_repo = http://127.0.0.1:4321/hbb\nemu_install = fceugx\n"))
        self.assertFalse(sync_apps.exists())
        ini = (self.user / "SDSync/newlauncher/emulators.ini").read_text(encoding="utf-8")
        nes = ini.split("[nes]")[1].split("[")[0]
        self.assertIn("dol = sd:/apps/fceugx/boot.dol", nes)
        self.assertIn("dol = sd:/apps/argv_echo/boot.dol", ini.split("[snes]")[1])
        self.assertEqual(fixture_ini.read_bytes(), before)

    def test_cover_server_serves_files(self):
        import urllib.request
        from dolphin_smoke import start_cover_server
        root = self.tmp / "srv"
        (root / "EN").mkdir(parents=True)
        (root / "EN/X.png").write_bytes(b"abc")
        server, port = start_cover_server(root)
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/EN/X.png", timeout=5) as r:
                self.assertEqual(r.read(), b"abc")
        finally:
            server.shutdown()
            server.server_close()

    def test_smoke_without_autostart_keeps_config(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        self._run_smoke("2")
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertNotIn("[debug]", text)

    def test_smoke_reports_missing_dolphin(self):
        self.assertEqual(smoke_main(["--dolphin", str(self.tmp / "fehlt.exe"), "--dol", str(self.dol),
                                     "--user-dir", str(self.user)]), 2)

    def test_smoke_reports_missing_dol(self):
        self.assertEqual(smoke_main(["--dolphin", sys.executable, "--dol", str(self.tmp / "fehlt.dol"),
                                     "--user-dir", str(self.user)]), 2)

    # Fake-Dolphin: dolphin_command ergibt `python -u <user_dir> -b -e <dol>`; Python führt
    # dann <user_dir>/__main__.py aus. So läuft der echte Smoke-Ablauf ohne Dolphin.
    def _fake_dolphin(self, body: str) -> None:
        self.user.mkdir(parents=True, exist_ok=True)
        (self.user / "__main__.py").write_text(body, encoding="utf-8")

    def _run_smoke(self, timeout: str) -> int:
        return smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol),
                           "--user-dir", str(self.user), "--timeout", timeout])

    def test_smoke_accepts_multiple_markers(self):
        self._fake_dolphin(
            "import pathlib, time\n"
            "log = pathlib.Path(__file__).parent / 'Logs' / 'dolphin.log'\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            "log.write_text('OSREPORT: NL_READY\\n')\n"
            "time.sleep(60)\n")
        self.assertEqual(smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol),
                                     "--user-dir", str(self.user), "--timeout", "2",
                                     "--marker", "NL_READY", "--marker", "NL_SELFTEST_OK"]), 1)

    def test_smoke_reports_unstartable_dolphin(self):
        bad = self.tmp / "kaputt.exe"
        bad.write_bytes(b"x")
        self.assertEqual(smoke_main(["--dolphin", str(bad), "--dol", str(self.dol),
                                     "--user-dir", str(self.user)]), 2)

    def test_smoke_reports_locked_log(self):
        with mock.patch("dolphin_smoke.reset_log", side_effect=PermissionError("gesperrt")):
            self.assertEqual(self._run_smoke("1"), 2)

    def test_smoke_with_other_dol_keeps_installed_launcher(self):
        # Der Selbsttest darf den Launcher auf der virtuellen SD nicht ersetzen.
        boot = self.user / "SDSync/apps/newlauncher/boot.dol"
        boot.parent.mkdir(parents=True)
        boot.write_bytes(b"launcher")
        selftest = self.tmp / "nl_selftest.dol"
        selftest.write_bytes(b"selftest")
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(selftest),
                    "--user-dir", str(self.user), "--timeout", "2"])
        self.assertEqual(boot.read_bytes(), b"launcher")

    def test_smoke_with_fixtures_removes_old_index(self):
        index = self.user / "SDSync/newlauncher/cache/index.bin"
        index.parent.mkdir(parents=True)
        index.write_bytes(b"alt")
        self._fake_dolphin("raise SystemExit(0)\n")
        self._run_smoke("2")
        self.assertFalse(index.exists())
        self.assertTrue((self.user / "SDSync/newlauncher/config.ini").is_file())

    def test_smoke_without_fixtures_keeps_index(self):
        index = self.user / "SDSync/newlauncher/cache/index.bin"
        index.parent.mkdir(parents=True)
        index.write_bytes(b"alt")
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--no-fixtures"])
        self.assertTrue(index.exists())

    def test_smoke_ignores_marker_from_previous_run(self):
        log_path(self.user).parent.mkdir(parents=True)
        log_path(self.user).write_text("OSREPORT: NL_READY\n")
        self._fake_dolphin("raise SystemExit(0)\n")
        self.assertEqual(self._run_smoke("10"), 1)

    def test_smoke_succeeds_when_marker_appears(self):
        self._fake_dolphin(
            "import pathlib, time\n"
            "log = pathlib.Path(__file__).parent / 'Logs' / 'dolphin.log'\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            "log.write_text('OSREPORT: NL_READY\\n')\n"
            "time.sleep(60)\n")
        self.assertEqual(self._run_smoke("10"), 0)

    def test_smoke_times_out_and_terminates_hanging_dolphin(self):
        self._fake_dolphin(
            "import os, pathlib, time\n"
            "(pathlib.Path(__file__).parent / 'pid').write_text(str(os.getpid()))\n"
            "time.sleep(60)\n")
        t = time.monotonic()
        self.assertEqual(self._run_smoke("1"), 1)
        self.assertLess(time.monotonic() - t, 10)
        pid = int((self.user / "pid").read_text())
        self.assertFalse(process_alive(pid))


if __name__ == "__main__":
    unittest.main()
