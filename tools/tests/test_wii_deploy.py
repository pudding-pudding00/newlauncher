import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from wii_deploy import (DeployError, deploy_release, deploy_selftest, main, set_ini_value,  # noqa: E402
                        set_perf, wiiload)


def files(root: Path) -> set[str]:
    return {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}


class SetIniValueTest(unittest.TestCase):
    def test_set_ini_value_keeps_comments_and_crlf(self):
        text = "; Kopf\r\n[ui]\r\nmusic = 1 ; an\r\n\r\n[debug]\r\n; perf = 0\r\nautostart = x\r\n"
        out = set_ini_value(text, "debug", "perf", "1")
        self.assertEqual(out, "; Kopf\r\n[ui]\r\nmusic = 1 ; an\r\n\r\n[debug]\r\n; perf = 0\r\nautostart = x\r\n"
                              "perf = 1\r\n")
        self.assertEqual(set_ini_value(out, "debug", "perf", "0"), out.replace("perf = 1", "perf = 0"))

    def test_set_ini_value_inserts_before_next_section(self):
        text = "[debug]\nautostart = x\n\n[ui]\nmusic = 1\n"
        self.assertEqual(set_ini_value(text, "debug", "perf", "1"),
                         "[debug]\nautostart = x\nperf = 1\n\n[ui]\nmusic = 1\n")

    def test_set_ini_value_adds_section(self):
        self.assertEqual(set_ini_value("[ui]\nmusic = 1", "debug", "perf", "1"),
                         "[ui]\nmusic = 1\n\n[debug]\nperf = 1\n")
        self.assertEqual(set_ini_value("", "debug", "perf", "1"), "[debug]\nperf = 1\n")

    def test_set_ini_value_removes_key(self):
        text = "[debug]\nperf = 1\nautostart = x\n[ui]\nperf = 7\n"
        self.assertEqual(set_ini_value(text, "debug", "perf", None), "[debug]\nautostart = x\n[ui]\nperf = 7\n")
        self.assertEqual(set_ini_value("[ui]\n", "debug", "perf", None), "[ui]\n")


class DeployTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.sd = self.tmp / "sd"
        self.sd.mkdir()
        self.dol = self.tmp / "newlauncher.dol"
        self.dol.write_bytes(b"DOL")
        self.dist = self.tmp / "dist"
        (self.dist / "apps/newlauncher").mkdir(parents=True)
        (self.dist / "apps/newlauncher/meta.xml").write_text("<app/>", encoding="utf-8")
        (self.dist / "newlauncher").mkdir()
        (self.dist / "newlauncher/config.ini").write_text("[ui]\nmusic = 1\n", encoding="utf-8")
        self.patches = [mock.patch("wii_deploy.DIST_DIR", self.dist),
                        mock.patch("wii_deploy.ARGV_ECHO_DOL", self.tmp / "argv_echo.dol")]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in self.patches:
            p.stop()
        self._tmp.cleanup()

    def test_release_keeps_user_config(self):
        (self.sd / "apps").mkdir()
        cfg = self.sd / "newlauncher/config.ini"
        cfg.parent.mkdir()
        cfg.write_bytes(b"[ui]\r\nmusic = 0\r\n")
        (self.sd / "newlauncher/favorites.ini").write_bytes(b"x")
        deploy_release(self.sd, self.dol, init=False)
        self.assertEqual(cfg.read_bytes(), b"[ui]\r\nmusic = 0\r\n")
        self.assertEqual((self.sd / "newlauncher/favorites.ini").read_bytes(), b"x")
        self.assertEqual((self.sd / "apps/newlauncher/boot.dol").read_bytes(), b"DOL")
        self.assertTrue((self.sd / "apps/newlauncher/meta.xml").is_file())

    def test_release_creates_missing_config(self):
        deploy_release(self.sd, self.dol, init=True)
        self.assertEqual((self.sd / "newlauncher/config.ini").read_text(encoding="utf-8"), "[ui]\nmusic = 1\n")

    def test_release_copies_argv_echo_if_built(self):
        (self.tmp / "argv_echo.dol").write_bytes(b"ECHO")
        deploy_release(self.sd, self.dol, init=True)
        self.assertEqual((self.sd / "apps/argv_echo/boot.dol").read_bytes(), b"ECHO")

    def test_release_refuses_drive_without_apps(self):
        with self.assertRaises(DeployError):
            deploy_release(self.sd, self.dol, init=False)
        self.assertEqual(files(self.sd), set())

    def test_release_refuses_missing_dol_and_drive(self):
        with self.assertRaises(DeployError):
            deploy_release(self.sd, self.tmp / "fehlt.dol", init=True)
        self.assertEqual(files(self.sd), set())
        with self.assertRaises(DeployError):
            deploy_release(self.tmp / "X", self.dol, init=True)

    def test_set_perf(self):
        cfg = self.sd / "newlauncher/config.ini"
        cfg.parent.mkdir()
        cfg.write_bytes(b"; Nutzer\r\n[ui]\r\nmusic = 0\r\n")
        set_perf(self.sd, True)
        self.assertEqual(cfg.read_bytes(), b"; Nutzer\r\n[ui]\r\nmusic = 0\r\n\r\n[debug]\r\nperf = 1\r\n")
        set_perf(self.sd, False)
        self.assertEqual(cfg.read_bytes(), b"; Nutzer\r\n[ui]\r\nmusic = 0\r\n\r\n[debug]\r\n")

    def test_set_perf_creates_config(self):
        set_perf(self.sd, True)
        self.assertEqual((self.sd / "newlauncher/config.ini").read_text(encoding="utf-8"), "[debug]\nperf = 1\n")

    def test_selftest_refuses_card_with_library(self):
        for name in ("wbfs", "games", "roms", "newlauncher"):
            with self.subTest(name=name):
                (self.sd / name).mkdir()
                (self.sd / name / "keep").write_bytes(b"k")
                with self.assertRaises(DeployError):
                    deploy_selftest(self.sd, self.dol)
                self.assertEqual(files(self.sd), {f"{name}/keep"})
                (self.sd / name / "keep").unlink()
                (self.sd / name).rmdir()

    def test_selftest_layout(self):
        deploy_selftest(self.sd, self.dol)
        got = files(self.sd)
        self.assertIn("apps/nl_selftest/boot.dol", got)
        meta = (self.sd / "apps/nl_selftest/meta.xml").read_text(encoding="utf-8")
        self.assertIn("<name>newlauncher Selbsttest</name>", meta)
        self.assertIn("<ahb_access/>", meta)
        self.assertTrue(any(f.startswith("wbfs/Mario Kart Wii [RMCP01]/") for f in got))
        self.assertIn("newlauncher/config.ini", got)
        self.assertNotIn("apps/newlauncher/boot.dol", got)

    def test_wiiload_sets_env(self):
        with mock.patch("wii_deploy.subprocess.run") as run:
            run.return_value.returncode = 3
            self.assertEqual(wiiload(self.dol, "1.2.3.4"), 3)
        args, kwargs = run.call_args
        self.assertEqual(args[0][1], str(self.dol))
        self.assertTrue(args[0][0].lower().endswith("wiiload.exe"))
        self.assertEqual(kwargs["env"]["WIILOAD"], "tcp:1.2.3.4")

    def test_cli_error_exit_2(self):
        self.assertEqual(main([str(self.sd), "--dol", str(self.dol)]), 2)  # kein apps/, kein --init
        self.assertEqual(main([str(self.sd), "--init", "--dol", str(self.dol), "--perf", "on"]), 0)
        self.assertIn("perf = 1", (self.sd / "newlauncher/config.ini").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
