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

    def test_set_ini_value_bom_case_duplicates_and_cr_only(self):
        # UTF-8-BOM wird erkannt und bewahrt; Groß-/Kleinschreibung entspricht IniDoc::parse (case-sensitiv).
        bom_in = "\ufeff[debug]\nautostart = x\n"
        self.assertEqual(set_ini_value(bom_in, "debug", "perf", "1"), "\ufeff[debug]\nautostart = x\nperf = 1\n")

        mixed = "[Debug]\nPerf = 1\n"
        self.assertEqual(set_ini_value(mixed, "debug", "perf", "1"), "[Debug]\nPerf = 1\n\n[debug]\nperf = 1\n")

        dup = "[debug]\na = 1\n[ui]\nx = 1\n[debug]\nperf = 1\n"
        self.assertEqual(set_ini_value(dup, "debug", "perf", None), "[debug]\na = 1\n[ui]\nx = 1\n[debug]\n")
        self.assertEqual(set_ini_value("[debug]\nperf = 0\n[debug]\nperf = 2\n", "debug", "perf", "1"),
                         "[debug]\nperf = 1\n[debug]\n")

        cr_only = "[debug]\rautostart = x\r"
        self.assertEqual(set_ini_value(cr_only, "debug", "perf", "1"), "[debug]\rautostart = x\rperf = 1\r")


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
        self.assertIn("GNU GENERAL PUBLIC LICENSE", (self.sd / "apps/newlauncher/LICENSE.txt").read_text(encoding="utf-8"))
        self.assertIn("Apache License", (self.sd / "apps/newlauncher/THIRD_PARTY.txt").read_text(encoding="utf-8"))

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
        set_perf(self.sd, False)
        self.assertFalse((self.sd / "newlauncher/config.ini").exists())
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

    def test_release_refuses_full_card_before_copy(self):
        (self.sd / "apps/newlauncher").mkdir(parents=True)
        (self.sd / "apps/newlauncher/boot.dol").write_bytes(b"OLD")
        with mock.patch("wii_deploy.shutil.disk_usage", return_value=mock.Mock(free=2)):
            with self.assertRaises(DeployError) as ctx:
                deploy_release(self.sd, self.dol, init=False)
        self.assertIn("Platz", str(ctx.exception))
        self.assertEqual(files(self.sd), {"apps/newlauncher/boot.dol"})
        self.assertEqual((self.sd / "apps/newlauncher/boot.dol").read_bytes(), b"OLD")

    def test_release_keeps_old_dol_when_copy_fails(self):
        (self.sd / "apps/newlauncher").mkdir(parents=True)
        (self.sd / "apps/newlauncher/boot.dol").write_bytes(b"OLD")
        real = __import__("shutil").copy2

        def failing(src, dst, *a, **kw):
            if Path(src) == self.dol:
                Path(dst).write_bytes(b"")  # wie ENOSPC mitten im Schreiben
                raise OSError(28, "No space left on device")
            return real(src, dst, *a, **kw)
        with mock.patch("wii_deploy.shutil.copy2", side_effect=failing):
            with self.assertRaises(OSError):
                deploy_release(self.sd, self.dol, init=False)
        self.assertEqual((self.sd / "apps/newlauncher/boot.dol").read_bytes(), b"OLD")
        self.assertEqual(files(self.sd), {"apps/newlauncher/boot.dol"})

    def test_cli_wiiload_refuses_selftest(self):
        selftest_dol = self.tmp / "nl_selftest.dol"
        selftest_dol.write_bytes(b"DOL")
        with mock.patch("wii_deploy.subprocess.run") as run, mock.patch("wii_deploy.SELFTEST_DOL", selftest_dol):
            self.assertEqual(main(["--wiiload", "1.2.3.4", "--selftest"]), 2)
            self.assertEqual(main(["--wiiload", "1.2.3.4", "--dol", str(selftest_dol)]), 2)
            self.assertEqual(main([str(self.sd), "--wiiload", "1.2.3.4", "--dol", str(self.dol)]), 2)
            self.assertEqual(main(["--wiiload", "1.2.3.4", "--perf", "on", "--dol", str(self.dol)]), 2)
            self.assertEqual(main(["--wiiload", "1.2.3.4", "--init", "--dol", str(self.dol)]), 2)
        run.assert_not_called()

    def test_cli_error_exit_2(self):
        self.assertEqual(main([str(self.sd), "--dol", str(self.dol)]), 2)  # kein apps/, kein --init
        self.assertEqual(main([str(self.sd), "--init", "--dol", str(self.dol), "--perf", "on"]), 0)
        self.assertIn("perf = 1", (self.sd / "newlauncher/config.ini").read_text(encoding="utf-8"))


class EmulatorsCliTest(unittest.TestCase):
    """--emulators ohne Netz: urlopen liefert Liste und ZIP aus dem Speicher."""

    def setUp(self):
        import io
        import zipfile
        self._tmp = tempfile.TemporaryDirectory()
        self.sd = Path(self._tmp.name) / "sd"
        (self.sd / "apps").mkdir(parents=True)
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("apps/fceugx/boot.dol", b"FCE")
            z.writestr("apps/fceugx/meta.xml", b"<app/>")
        self.zip = buf.getvalue()
        self.list = (f"Homebrew 1 v0.3.9e\r\n=Emulators=\r\n"
                     f"fceugx 1793664000 8367 9 dol {len(self.zip)} 0 0 wwwwngcs . .\r\n"
                     "FCE Ultra GX\r\nTantric\r\n4.0.4\r\n9\r\nNES\r\nA port.\r\n").encode()
        self.urls: list[str] = []

    def tearDown(self):
        self._tmp.cleanup()

    def _urlopen(self, url, timeout=None):
        import io
        self.urls.append(url)
        if url == "http://repo.test/hbb/listv036.txt":
            return io.BytesIO(self.list)
        if url == "http://repo.test/hbb/fceugx/fceugx.zip":
            return io.BytesIO(self.zip)
        raise OSError(f"404 {url}")

    def _main(self, *args, answer="j"):
        with mock.patch("osc_emulators.urlopen", self._urlopen), mock.patch("builtins.input", return_value=answer):
            return main([str(self.sd), "--emulators", *args, "--repo", "http://repo.test/hbb/"])

    def test_answer_n_downloads_no_zip(self):
        self.assertEqual(self._main("fceugx", answer="n"), 1)
        self.assertEqual(self.urls, ["http://repo.test/hbb/listv036.txt"])
        self.assertEqual(files(self.sd), set())

    def test_yes_installs_and_second_run_skips_current(self):
        self.assertEqual(self._main("fceugx", "--yes"), 0)
        self.assertEqual((self.sd / "apps/fceugx/boot.dol").read_bytes(), b"FCE")
        self.assertIn("[fceugx]\ntimestamp = 1793664000\nversion = 4.0.4\n",
                      (self.sd / "newlauncher/emulators_installed.ini").read_text(encoding="utf-8"))
        self.urls.clear()
        self.assertEqual(self._main("fceugx", "--yes"), 0)  # aktuell: kein ZIP-Download
        self.assertEqual(self.urls, ["http://repo.test/hbb/listv036.txt"])

    def test_without_names_takes_catalog_entries_in_list(self):
        self.assertEqual(self._main("--yes"), 0)  # nur fceugx steht in der Liste: der Rest wird gemeldet
        self.assertIn("http://repo.test/hbb/fceugx/fceugx.zip", self.urls)

    def test_unknown_name_and_bad_combinations_exit_2(self):
        self.assertEqual(self._main("zsnes", "--yes"), 2)
        self.assertEqual(self.urls, [])
        with mock.patch("osc_emulators.urlopen", self._urlopen):
            self.assertEqual(main([str(self.sd), "--emulators", "--selftest"]), 2)
            self.assertEqual(main(["--emulators", "--yes"]), 2)  # Laufwerk fehlt
        self.assertEqual(self.urls, [])

    def test_wrong_zip_size_fails_and_keeps_card(self):
        self.zip += b"x"
        self.assertEqual(self._main("fceugx", "--yes"), 2)
        self.assertEqual(files(self.sd), set())


class ConsoleEncodingTest(unittest.TestCase):
    def test_cli_on_cp1252_console(self):
        # Windows-Konsole: stdout ist cp1252, die Meldungen enthalten „→“.
        import os
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            dol = Path(d) / "x.dol"
            dol.write_bytes(b"DOL")
            (Path(d) / "sd").mkdir()
            env = dict(os.environ, PYTHONIOENCODING="cp1252")
            r = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "wii_deploy.py"),
                                str(Path(d) / "sd"), "--selftest", "--dol", str(dol)],
                               env=env, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr.decode("utf-8", "replace"))

if __name__ == "__main__":
    unittest.main()
