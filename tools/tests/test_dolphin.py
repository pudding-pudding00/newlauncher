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
from dolphin import (dolphin_command, log_path, reset_log, sd_sync_dir, wait_for_markers,  # noqa: E402
                     write_dev_config)
from dolphin_smoke import main as smoke_main  # noqa: E402
from dolphin_smoke import missing_ini_lines, sysinfo_problems  # noqa: E402


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

    def test_dolphin_command_with_sd_image_uses_raw_card(self):
        # M16: SD-Karte als Rohgerät; ohne Ordner-Sync liest Dolphin das Image direkt.
        cmd = dolphin_command(Path("D.exe"), self.user, Path("a.dol"), sd_image=Path("x.raw"))
        self.assertEqual(cmd, ["D.exe", "-u", str(self.user), "-b", "-C", "Dolphin.DSP.Volume=0",
                               "-C", "Dolphin.General.WiiSDCardPath=x.raw",
                               "-C", "Dolphin.Core.WiiSDCardEnableFolderSync=False", "-e", "a.dol"])

    def test_dolphin_command_with_cheats_disables_hbreload_hook(self):
        # M20: Ohne Cheats ersetzt Dolphin 0x80001800 durch HLE "HBReload" und hält an; der Rückkehr-Stub liefe nie.
        cmd = dolphin_command(Path("D.exe"), self.user, Path("a.dol"), cheats=True)
        self.assertEqual(cmd, ["D.exe", "-u", str(self.user), "-b", "-C", "Dolphin.DSP.Volume=0",
                               "-C", "Dolphin.Core.EnableCheats=True", "-e", "a.dol"])

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

        def fake_start(root, ssl_context=None):
            root = Path(root)
            seen["en"] = (root / "EN/RMCP01.png").read_bytes()[:8]
            seen["us"] = (root / "US/GM8P01.png").read_bytes()[:8]
            seen["nes"] = (root / "Nintendo_-_Nintendo_Entertainment_System/master/Named_Boxarts/"
                           "Zelda (Europe) (Rev 1).png").read_bytes()[:8]  # M22: Retro-Cover
            seen["count"] = len([p for p in root.rglob("*") if p.is_file()])
            return mock.Mock(), 12345

        with mock.patch("dolphin_smoke.start_cover_server", side_effect=fake_start) as start:
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server"])
        start.assert_called_once()
        self.assertEqual(seen["en"], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(seen["us"], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(seen["nes"], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(seen["count"], 3)
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\ncover_url = http://127.0.0.1:12345\nfetch_covers = 1\n"))
        self.assertIn("[covers]\nregion = auto\nauto = 0\n", text)  # M21: Smokes laden nicht von selbst

    def test_smoke_cover_auto_turns_on_auto_without_fetch_covers(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        with mock.patch("dolphin_smoke.start_cover_server", return_value=(mock.Mock(), 12345)):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server", "--cover-auto"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("[covers]\nregion = auto\nauto = 1\n", text)
        self.assertTrue(text.endswith("[debug]\ncover_url = http://127.0.0.1:12345\n"))

    def test_smoke_without_cover_auto_resets_leftover_auto_1(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        with mock.patch("dolphin_smoke.start_cover_server", return_value=(mock.Mock(), 12345)):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server", "--cover-auto"])
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--no-fixtures"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("\nauto = 0\n", text)  # M21-Review: kein Autostart aus einem früheren Lauf
        self.assertNotIn("\nauto = 1\n", text)

    def test_smoke_cover_auto_needs_cover_server(self):
        with self.assertRaises(SystemExit):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--cover-auto"])

    def test_smoke_cheat_server_serves_generated_codes_or_html(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        for mode, start in (("ok", b"RMCP01\n"), ("html", b"<!DOCTYPE html>")):
            seen = {}

            def fake_start(root, ssl_context=None):
                seen["body"] = (Path(root) / "codes/RMCP01.txt").read_bytes()
                return mock.Mock(), 23456

            stale = self.user / "SDSync/newlauncher/codes/RMCP01.txt"
            stale.parent.mkdir(parents=True, exist_ok=True)
            stale.write_text("alt", encoding="utf-8")
            with mock.patch("dolphin_smoke.start_cover_server", side_effect=fake_start):
                smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                            "--timeout", "2", "--cheat-server", mode])
            self.assertTrue(seen["body"].startswith(start), mode)
            self.assertFalse(stale.exists(), "alte Codeliste muss vor dem Lauf weg")
            text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
            self.assertTrue(text.endswith("[debug]\ncheat_url = http://127.0.0.1:23456/codes/{id6}.txt\n"
                                          "cheat_fetch = RMCP01\n"), text)

    def test_sysinfo_problems_accepts_clean_text(self):
        text = "newlauncher 0.13.0\nPlattform: Wii\nSystem-Menü: 4.3E (514)\nNetzwerk: verbunden\n"
        self.assertEqual(sysinfo_problems(text), [])
        self.assertEqual(sysinfo_problems("Platform: vWii\n"), [])

    def test_missing_ini_lines_ignores_spacing_and_reports_absent(self):
        text = "[game RMGP01]\r\n  video =   pal60 \r\n"
        self.assertEqual(missing_ini_lines(text, ["[game RMGP01]", "video = pal60"]), [])
        self.assertEqual(missing_ini_lines(text, ["video = ntsc"]), ["video = ntsc"])

    def test_sysinfo_problems_flags_mac_ip_and_missing_platform(self):
        self.assertTrue(sysinfo_problems("Plattform: Wii\nMAC 00:17:AB:00:00:01\n"))
        self.assertTrue(sysinfo_problems("Plattform: Wii\nIP 192.168.0.12\n"))
        self.assertTrue(sysinfo_problems("nur Text\n"))

    def test_smoke_expect_sysinfo_appends_debug_key(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--expect-sysinfo"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("[debug]\nsysinfo_save = 1\n"))
        self.assertFalse((self.user / "SDSync/newlauncher/sysinfo.txt").exists())

    def test_smoke_fresh_data_writes_only_debug_ini(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        rc = smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                         "--timeout", "2", "--fresh-data", "--debug", "perf=1"])
        self.assertEqual(rc, 1)  # NL_WIZARD done fehlt
        data = self.user / "SDSync/newlauncher"
        self.assertFalse((data / "config.ini").exists())
        self.assertEqual((data / "debug.ini").read_text(encoding="utf-8"), "[debug]\nwizard = accept\nperf = 1\n"
                         "cover_url = http://127.0.0.1:9\n")  # M21: Cover-Autostart nicht ins Internet

    def test_smoke_without_fresh_data_removes_stale_debug_ini(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        data = self.user / "SDSync/newlauncher"
        data.mkdir(parents=True, exist_ok=True)
        (data / "debug.ini").write_text("[debug]\nwizard = accept\n", encoding="utf-8")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2"])
        self.assertFalse((data / "debug.ini").exists())
        self.assertTrue((data / "config.ini").exists())

    def test_smoke_games_ini_and_config_append(self):
        # M14: Spieloptionen für Smokes; ein wörtliches \n im Argument wird zum Zeilenumbruch.
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--games-ini", "[game RMCP01]\\nvideo = pal60",
                    "--config-append", "[gc]\\nprogressive = 1", "--debug", "perf=1"])
        data = self.user / "SDSync/newlauncher"
        self.assertEqual((data / "games.ini").read_text(encoding="utf-8"), "[game RMCP01]\nvideo = pal60\n")
        text = (data / "config.ini").read_text(encoding="utf-8")
        self.assertTrue(text.endswith("\n[gc]\nprogressive = 1\n[debug]\nperf = 1\n"), text[-80:])

    def test_smoke_without_games_ini_removes_stale_one(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        data = self.user / "SDSync/newlauncher"
        data.mkdir(parents=True, exist_ok=True)
        (data / "games.ini").write_text("[game RMCP01]\nvideo = pal60\n", encoding="utf-8")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2"])
        self.assertFalse((data / "games.ini").exists())

    # --- M17: --categories schreibt newlauncher/categories.ini; ohne die Option wird sie entfernt ---
    def test_smoke_categories_writes_file(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--categories", "[Party]\\nnes/zelda (europe) (rev 1).nes"])
        text = (self.user / "SDSync/newlauncher/categories.ini").read_text(encoding="utf-8")
        self.assertEqual(text, "[Party]\nnes/zelda (europe) (rev 1).nes\n")

    def test_smoke_without_categories_removes_stale_file(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        data = self.user / "SDSync/newlauncher"
        data.mkdir(parents=True, exist_ok=True)
        (data / "categories.ini").write_text("[Alt]\nnes/a\n", encoding="utf-8")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2"])
        self.assertFalse((data / "categories.ini").exists())

    # --- M17: stats.ini eines früheren Laufs wird entfernt; --expect-stats prüft sie im SD-Image ---
    def test_smoke_removes_stale_stats_ini(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        data = self.user / "SDSync/newlauncher"
        data.mkdir(parents=True, exist_ok=True)
        (data / "stats.ini").write_text("[RMCP01]\nstarts = 7\nlast = 1\n", encoding="utf-8")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2"])
        self.assertFalse((data / "stats.ini").exists())

    def test_smoke_expect_stats_fails_without_sd_image(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        self.assertEqual(smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol),
                                     "--user-dir", str(self.user), "--timeout", "2",
                                     "--expect-stats", "[RMCP01]", "--expect-stats", "starts = 1"]), 1)

    # --- M17: --theme-pack legt ein Fixture-Theme an und verlangt genau eine NL_THEME_BAD-Zeile ---
    def test_smoke_theme_pack_writes_fixture_and_config(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2", "--theme-pack", "smoke"])
        data = self.user / "SDSync/newlauncher"
        ini = (data / "themes/smoke/theme.ini").read_text(encoding="utf-8")
        self.assertIn("bar = #123456", ini)
        self.assertIn("bar_border = fff", ini)
        self.assertIn("[theme]\nname = smoke\n", (data / "config.ini").read_text(encoding="utf-8"))

    def test_smoke_without_theme_pack_removes_stale_themes(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        stale = self.user / "SDSync/newlauncher/themes/alt"
        stale.mkdir(parents=True, exist_ok=True)
        (stale / "theme.ini").write_text("[light]\n", encoding="utf-8")
        smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                    "--timeout", "2"])
        self.assertFalse((self.user / "SDSync/newlauncher/themes").exists())

    def _theme_run(self, logged: str) -> int:
        self._fake_dolphin(
            "import pathlib, time\n"
            "log = pathlib.Path(__file__).parent / 'Logs' / 'dolphin.log'\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            f"log.write_text({logged!r})\n"
            "time.sleep(60)\n")
        return smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                           "--timeout", "5", "--theme-pack", "smoke", "--marker", "NL_THEME smoke"])

    def test_smoke_theme_pack_accepts_exactly_one_bad_line(self):
        self.assertEqual(self._theme_run("NL_THEME smoke light.bar=#123456\nNL_THEME_BAD light.bar_border\n"), 0)

    def test_smoke_theme_pack_fails_on_two_bad_lines(self):
        self.assertEqual(self._theme_run("NL_THEME smoke light.bar=#123456\nNL_THEME_BAD light.bar_border\n"
                                         "NL_THEME_BAD light.accent\n"), 1)

    def test_smoke_tls_requires_a_server(self):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit) as cm:
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--tls"])
        self.assertEqual(cm.exception.code, 2)

    def _fake_make_ca(self, seen):
        def make(root):
            root = Path(root)
            root.mkdir(parents=True, exist_ok=True)
            paths = {"ca": root / "ca.pem", "cert": root / "server.pem", "key": root / "server.key"}
            for p in paths.values():
                p.write_bytes(b"-----BEGIN CERTIFICATE-----\n" + str(root).encode() + b"\n")
            seen.append(paths["ca"].read_bytes())
            return paths
        return make

    def test_smoke_tls_uses_https_localhost_and_test_ca(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        seen_ca, seen_ctx, sd_ca = [], [], []

        def fake_start(root, ssl_context=None):
            seen_ctx.append(ssl_context)
            return mock.Mock(), 4443

        def fake_popen(*a, **k):  # die SD-Kopie der CA existiert, solange Dolphin läuft
            sd_ca.append((self.user / "SDSync/newlauncher/test_ca.pem").read_bytes())
            raise OSError("kein Dolphin im Test")

        with mock.patch("dolphin_smoke.start_cover_server", side_effect=fake_start), \
                mock.patch("dolphin_smoke.make_test_ca", side_effect=self._fake_make_ca(seen_ca)), \
                mock.patch("dolphin_smoke.ssl_server_context", return_value="CTX"), \
                mock.patch("dolphin_smoke.subprocess.Popen", side_effect=fake_popen):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--cover-server", "--tls"])
        self.assertEqual(seen_ctx, ["CTX"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("cover_url = https://localhost:4443\n", text)
        self.assertIn("tls_ca = sd:/newlauncher/test_ca.pem\n", text)
        self.assertNotIn("http://", text.partition("[debug]")[2])
        self.assertEqual(len(seen_ca), 1)
        self.assertEqual(sd_ca, [seen_ca[0]])
        self.assertFalse((self.user / "SDSync/newlauncher/test_ca.pem").exists())  # danach gelöscht

    def test_smoke_tls_wrong_ca_copies_a_foreign_ca(self):
        self._fake_dolphin("raise SystemExit(0)\n")
        seen_ca, sd_ca = [], []

        def fake_popen(*a, **k):
            sd_ca.append((self.user / "SDSync/newlauncher/test_ca.pem").read_bytes())
            raise OSError("kein Dolphin im Test")

        with mock.patch("dolphin_smoke.start_cover_server", return_value=(mock.Mock(), 4443)), \
                mock.patch("dolphin_smoke.make_test_ca", side_effect=self._fake_make_ca(seen_ca)), \
                mock.patch("dolphin_smoke.ssl_server_context", return_value="CTX"), \
                mock.patch("dolphin_smoke.subprocess.Popen", side_effect=fake_popen), \
                mock.patch("dolphin_smoke.ARGV_ECHO_DOL", self.dol):
            smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                        "--timeout", "2", "--emu-server", "--tls-wrong-ca"])
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("emu_repo = https://localhost:4443/hbb\n", text)
        self.assertEqual(len(seen_ca), 2)  # Server-CA und eine fremde CA
        self.assertEqual(sd_ca, [seen_ca[1]])

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

        def fake_start(root, ssl_context=None):
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

    # --- M9: --expect-frag gleicht NL_FRAG mit fat_frag.py ab ---
    FRAG_REL = "Super Mario Galaxy [RMGP01]/RMGP01.iso"
    FRAG_LINE = "NL_FRAG dev=sd part_lba=2048 n=2 size=3 crc=6dabbf0a first=0:3182:2"

    def _frag_run(self, logged: str, fat16: bool = False) -> int:
        from test_fat_frag import build_image  # noqa: PLC0415 - nur hier gebraucht
        image = self.user / "Load/WiiSD.raw"
        image.parent.mkdir(parents=True, exist_ok=True)
        build_image(image, fat16=fat16)
        self._fake_dolphin(
            "import pathlib, time\n"
            "log = pathlib.Path(__file__).parent / 'Logs' / 'dolphin.log'\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            f"log.write_text('OSREPORT: NL_READY\\nOSREPORT: {logged}\\n')\n"
            "time.sleep(60)\n")
        return smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                           "--timeout", "5", "--expect-frag", self.FRAG_REL])

    def test_smoke_expect_frag_matches(self):
        self.assertEqual(self._frag_run(self.FRAG_LINE), 0)

    def test_smoke_expect_frag_mismatch_fails(self):
        self.assertEqual(self._frag_run(self.FRAG_LINE.replace("crc=6dabbf0a", "crc=00000000")), 1)

    def test_smoke_expect_frag_skips_non_fat32(self):
        with mock.patch("sys.stdout") as out:
            self.assertEqual(self._frag_run("NL_FRAG irgendwas", fat16=True), 0)
        printed = "".join(str(c.args[0]) for c in out.write.call_args_list if c.args)
        self.assertIn("SMOKE HINWEIS: SD-Image nicht FAT32, Abgleich übersprungen", printed)

    # --- M16: --sd-disk baut ein Platten-Image aus der SD und einer NTFS-Partition ---
    def _sd_disk_run(self, logged: list[str], layout: str = "fat32,ntfs", extra: list[str] | None = None):
        calls = []
        self.ntfs_files = {}

        def fake_make_image(out, specs, native=False):
            calls.append((out, specs, (specs[1][1] / "nl_ntfs.txt").is_file() if len(specs) > 1 else None))
            if len(specs) > 1:
                root = specs[1][1]
                self.ntfs_files = {f.relative_to(root).as_posix(): f.read_bytes() for f in root.rglob("*") if f.is_file()}
            out.write_bytes(b"x" * 512)
            return 0

        body = ['OSREPORT: NL_READY', '<argv>'] + logged
        self._fake_dolphin(
            "import pathlib, sys, time\n"
            f"lines = {body!r}\n"
            "lines[1] = ' '.join(sys.argv)\n"
            "log = pathlib.Path(__file__).parent / 'Logs' / 'dolphin.log'\n"
            "log.parent.mkdir(parents=True, exist_ok=True)\n"
            "log.write_text(''.join(x + chr(10) for x in lines))\n"
            "time.sleep(60)\n")
        with mock.patch("dolphin_smoke.make_image", side_effect=fake_make_image):
            rc = smoke_main(["--dolphin", sys.executable, "--dol", str(self.dol), "--user-dir", str(self.user),
                             "--timeout", "5", "--sd-disk", layout] + (extra or []))
        return rc, calls

    def test_smoke_sd_disk_builds_image_from_sd(self):
        rc, calls = self._sd_disk_run(["OSREPORT: NL_DEV usb fat mounted lba=2048",
                                       "OSREPORT: NL_DEV usb2 ntfs mounted ro lba=133120"])
        self.assertEqual(rc, 0)
        out, specs, ntfs_filled = calls[0]
        self.assertEqual(out, self.user.resolve() / "sd_disk.raw")
        self.assertEqual(specs[0], ("fat32", sd_sync_dir(self.user.resolve())))
        self.assertEqual(specs[1][0], "ntfs")
        self.assertTrue(ntfs_filled)
        text = (self.user / "SDSync/newlauncher/config.ini").read_text(encoding="utf-8")
        self.assertIn("sd_as_disk = 1", text)
        # Fixture-config.ini sagt library = sd; ohne „auto“ fragt der Launcher die Platte nie ab.
        self.assertIn("[library]\nsources = auto\n[debug]", text)
        log = (self.user / "Logs/dolphin.log").read_text()
        self.assertIn("WiiSDCardPath=" + str(out), log)

    def test_smoke_sd_disk_requires_ntfs_marker(self):
        rc, _ = self._sd_disk_run(["OSREPORT: NL_DEV usb fat mounted lba=2048"])
        self.assertEqual(rc, 1)

    def test_smoke_ntfs_iso_puts_contiguous_wii_iso_on_ntfs(self):
        rc, _ = self._sd_disk_run(["OSREPORT: NL_DEV usb fat mounted lba=2048",
                                   "OSREPORT: NL_DEV usb2 ntfs mounted ro lba=133120"],
                                  extra=["--ntfs-iso", "RNLE01:1M", "--ntfs-iso", "RNLR01:512"])
        self.assertEqual(rc, 0)
        big = self.ntfs_files["wbfs/Test [RNLE01]/RNLE01.iso"]
        self.assertEqual(len(big), 1 << 20)
        self.assertEqual(big[:6], b"RNLE01")
        self.assertEqual(big[0x18:0x1C], bytes.fromhex("5D1C9EA3"))  # Wii-Magic
        # Kein Sektor nur aus Nullen: cp/ntfs-3g dürfen keine Löcher (sparse) daraus machen.
        self.assertTrue(all(any(big[i:i + 512]) for i in range(0, len(big), 512)))
        self.assertEqual(len(self.ntfs_files["wbfs/Test [RNLR01]/RNLR01.iso"]), 512)  # klein → resident

    def test_smoke_ntfs_iso_rejects_bad_spec(self):
        for spec in ["RNLE0:1M", "RNLE01", "RNLE01:abc", "RNLE01:100"]:
            rc, calls = self._sd_disk_run([], extra=["--ntfs-iso", spec])
            self.assertEqual(rc, 2, spec)
            self.assertEqual(calls, [], spec)

    def test_smoke_ntfs_iso_needs_ntfs_partition(self):
        rc, calls = self._sd_disk_run([], layout="fat32", extra=["--ntfs-iso", "RNLE01:1M"])
        self.assertEqual(rc, 2)
        self.assertEqual(calls, [])

    def test_smoke_wbfs_iso_goes_to_wbfs_partition_in_order(self):
        rc, _ = self._sd_disk_run(["OSREPORT: NL_DEV usb fat mounted lba=2048",
                                   "OSREPORT: NL_DEV usb2 wbfs mounted ro lba=133120"], layout="fat32,wbfs",
                                  extra=["--wbfs-iso", "RNLA01:1M", "--wbfs-iso", "RNLB01:1M:broken"])
        self.assertEqual(rc, 0)
        self.assertEqual(sorted(self.ntfs_files), ["00_RNLA01.iso", "01_RNLB01.broken.iso"])
        self.assertEqual(self.ntfs_files["00_RNLA01.iso"][:6], b"RNLA01")
        self.assertEqual(len(self.ntfs_files["01_RNLB01.broken.iso"]), 1 << 20)

    def test_smoke_wbfs_layout_expects_ro_mount(self):
        rc, _ = self._sd_disk_run(["OSREPORT: NL_DEV usb fat mounted lba=2048",
                                   "OSREPORT: NL_DEV usb2 wbfs skipped lba=133120"], layout="fat32,wbfs")
        self.assertNotEqual(rc, 0)

    def test_smoke_wbfs_iso_needs_wbfs_partition(self):
        for extra in (["--wbfs-iso", "RNLA01:1M"], ["--wbfs-iso", "RNLA01:1M:kaputt"]):
            rc, calls = self._sd_disk_run([], layout="fat32,ntfs" if len(extra[1]) < 12 else "fat32,wbfs", extra=extra)
            self.assertEqual(rc, 2, extra)
            self.assertEqual(calls, [], extra)

    def test_smoke_sd_disk_first_partition_must_be_fat32(self):
        rc, calls = self._sd_disk_run([], layout="ntfs,fat32")
        self.assertEqual(rc, 2)
        self.assertEqual(calls, [])

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
