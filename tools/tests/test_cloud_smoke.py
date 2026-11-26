"""Tests für tools/cloud_smoke.py (M27): Einheit-IDs wie im Launcher, Plan je Schritt, Geheimnis-grep."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import cloud_fake  # noqa: E402
import cloud_smoke  # noqa: E402


class CloudSmokeTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.user = Path(self._tmp.name) / "user"
        emus = self.user / "SDSync/newlauncher/emulators.ini"
        emus.parent.mkdir(parents=True)
        emus.write_text("[gb]\ndol = sd:/apps/vbagx/boot.dol\n", encoding="utf-8")

    def tearDown(self):
        self._tmp.cleanup()

    def test_slug_like_core(self):
        self.assertEqual(cloud_smoke.slug("Tetris (World) (Rev 1)"), "tetris-world-rev-1")
        self.assertEqual(cloud_smoke.slug("!!!"), "rom")
        self.assertEqual(cloud_smoke.slug("a" * 50), "a" * 40)
        self.assertEqual(cloud_smoke.slug("a" * 39 + " b"), "a" * 39)  # Bindestrich am Ende fällt weg

    def test_rom_unit_uses_crc_of_key(self):
        import zlib
        key = "gb/Tetris (World) (Rev 1).gb"
        self.assertEqual(cloud_smoke.rom_unit(key),
                         "gb-tetris-world-rev-1-%08x" % (zlib.crc32(key.encode()) & 0xFFFFFFFF))

    def test_save_zip_meta_first_and_hash(self):
        data = cloud_smoke.save_zip("u", {"a.sav": b"x"})
        meta = cloud_fake.zip_meta(data)
        self.assertEqual(meta["unit"], "u")
        self.assertEqual(meta["hash"], cloud_fake.content_hash({"a.sav": b"x"}))
        import io
        import zipfile
        self.assertEqual(zipfile.ZipFile(io.BytesIO(data)).namelist()[0], cloud_fake.META_NAME)

    def test_make_fake_sets_failure_modes(self):
        self.assertTrue(cloud_smoke.make_fake("refresh").expire_access_once)
        self.assertTrue(cloud_smoke.make_fake("offline").offline_after_link)
        self.assertFalse(cloud_smoke.make_fake("push").expire_access_once)
        with self.assertRaises(ValueError):
            cloud_smoke.make_fake("quatsch")

    def test_every_step_links_first(self):
        for step in cloud_smoke.STEPS:
            if step in cloud_smoke.DROPBOX_ONLY:
                continue
            plan = cloud_smoke.prepare(step, cloud_smoke.make_fake(step), self.user)
            self.assertEqual(plan.markers[0], "NL_CLOUD_LINKED drive", step)
            self.assertIn("cloud_device_auto = 1", plan.debug_lines, step)
            cloud_smoke.cleanup(self.user)

    def test_refresh_and_offline_markers(self):
        plan = cloud_smoke.prepare("refresh", cloud_smoke.make_fake("refresh"), self.user)
        self.assertIn("NL_CLOUD_REFRESH ok", plan.markers)
        plan = cloud_smoke.prepare("offline", cloud_smoke.make_fake("offline"), self.user)
        self.assertIn(f"NL_CLOUD {plan.unit} skip offline", plan.markers)

    def test_leaked_secrets_finds_tokens_and_client_secret(self):
        fake = cloud_fake.FakeDrive()
        _, dc = fake.device_code({"client_id": "x"})
        _, tok = fake.token({"client_id": "x", "device_code": dc["device_code"],
                             "grant_type": cloud_fake.DEVICE_GRANT})
        self.assertEqual(cloud_smoke.leaked_secrets("NL_CLOUD_LINKED drive\n", fake), [])
        self.assertEqual(len(cloud_smoke.leaked_secrets("x " + tok["access_token"], fake)), 1)
        self.assertEqual(len(cloud_smoke.leaked_secrets("y " + dc["device_code"], fake)), 1)
        self.assertEqual(len(cloud_smoke.leaked_secrets(cloud_smoke.CLIENT_SECRET, fake)), 1)

    def test_post_check_push_compares_hash(self):
        fake = cloud_smoke.make_fake("push")
        plan = cloud_smoke.prepare("push", fake, self.user)
        fake.refresh.add("1//x")  # angemeldet
        problems = cloud_smoke.post_check(plan, self.user, "", lambda rel: b"")
        self.assertIn(f"Fake: {plan.unit}.zip fehlt", problems)
        import save_smoke
        fake.seed(plan.unit, cloud_smoke.save_zip(plan.unit, {"Tetris (World) (Rev 1).sav": save_smoke.BYTES_A}))
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""), [])

    def test_post_check_counts_refreshes(self):
        fake = cloud_smoke.make_fake("link")
        plan = cloud_smoke.prepare("link", fake, self.user)
        fake.refresh.add("1//x")
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""), [])
        fake.refreshes = 1  # ein Refresh ohne 401: Tokens wurden nicht im Speicher gehalten
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""),
                         ["Fake: 1 Refreshes statt 0"])
        fake = cloud_smoke.make_fake("refresh")
        plan = cloud_smoke.prepare("refresh", fake, self.user)
        fake.refresh.add("1//x")
        self.assertIn("Fake: 0 Refreshes statt 1", cloud_smoke.post_check(plan, self.user, "", lambda rel: b""))

    def test_post_check_pull_reads_sd(self):
        import save_smoke
        fake = cloud_smoke.make_fake("pull")
        plan = cloud_smoke.prepare("pull", fake, self.user)
        fake.refresh.add("1//x")
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: save_smoke.BYTES_B), [])
        self.assertEqual(len(cloud_smoke.post_check(plan, self.user, "", lambda rel: save_smoke.BYTES_A)), 1)

    def test_m28_onedrive_plan_and_new_steps(self):
        fake = cloud_smoke.make_fake("link", "onedrive")
        self.assertIsInstance(fake, cloud_fake.FakeOneDrive)
        plan = cloud_smoke.prepare("link", fake, self.user)
        self.assertEqual(plan.markers[0], "NL_CLOUD_LINKED onedrive")
        self.assertIn("provider = onedrive", plan.config_append)
        self.assertIn("onedrive_client_id = ", plan.config_append)
        self.assertEqual(cloud_smoke.PROVIDERS[:2], ("drive", "onedrive"))
        for step in ("syncall", "switch", "unlink"):
            self.assertIn(step, cloud_smoke.STEPS)

    def test_m28_syncall_switch_unlink_markers(self):
        plan = cloud_smoke.prepare("syncall", cloud_smoke.make_fake("syncall"), self.user)
        self.assertIn("NL_CLOUD_SYNCALL pulled=1 pushed=1", " ".join(plan.markers))
        self.assertIsNotNone(plan.fake.file_bytes(cloud_smoke.NAND_UNIT))  # nur in der Cloud
        cloud_smoke.cleanup(self.user)
        plan = cloud_smoke.prepare("switch", cloud_smoke.make_fake("switch", "onedrive"), self.user)
        self.assertIn("cloud_switch = wii", plan.debug_lines)
        self.assertIn("NL_CLOUD_SWITCH uploaded=1 conflict=0", plan.markers)
        cloud_smoke.cleanup(self.user)
        plan = cloud_smoke.prepare("unlink", cloud_smoke.make_fake("unlink"), self.user)
        self.assertIn("cloud_unlink = 1", plan.debug_lines)
        self.assertIn("NL_CLOUD_UNLINKED revoke=ok", plan.markers)
        cloud_smoke.cleanup(self.user)
        plan = cloud_smoke.prepare("unlink", cloud_smoke.make_fake("unlink", "onedrive"), self.user)
        self.assertIn("NL_CLOUD_UNLINKED revoke=none", plan.markers)

    def test_m28_post_check_unlink_counts_revoke(self):
        fake = cloud_smoke.make_fake("unlink")
        plan = cloud_smoke.prepare("unlink", fake, self.user)
        self.assertIn("Fake: 0 Widerrufe statt 1", cloud_smoke.post_check(plan, self.user, "", lambda rel: b""))
        fake.revokes = 1
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""), [])

    def test_m28_post_check_onedrive_push_reads_description(self):
        import save_smoke
        fake = cloud_smoke.make_fake("push", "onedrive")
        plan = cloud_smoke.prepare("push", fake, self.user)
        fake.refresh.add("x")
        fake.seed(plan.unit, cloud_smoke.save_zip(plan.unit, {"Tetris (World) (Rev 1).sav": save_smoke.BYTES_A}))
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""), [])


    # --- M29: Dropbox mit Kopplungsseite ---
    def test_m29_dropbox_plan_pairs_with_fixed_token(self):
        fake = cloud_smoke.make_fake("link", "dropbox")
        self.assertIsInstance(fake, cloud_fake.FakeDropbox)
        plan = cloud_smoke.prepare("link", fake, self.user)
        self.assertEqual(plan.markers[0], "NL_CLOUD_LINKED dropbox")
        self.assertIn("provider = dropbox", plan.config_append)
        self.assertIn(f"dropbox_app_key = {cloud_smoke.DBX_APP_KEY}", plan.config_append)
        self.assertIn(f"cloud_pair_token = {cloud_smoke.PAIR_TOKEN}", plan.debug_lines)
        self.assertRegex(cloud_smoke.PAIR_TOKEN, r"^[0-9a-f]{32}$")
        self.assertIn("NL_PAIR listen 4301", plan.markers)
        self.assertIn("NL_PAIR ok", plan.markers)
        self.assertTrue(plan.pair)
        self.assertFalse(cloud_smoke.prepare("link", cloud_smoke.make_fake("link"), self.user).pair)

    def test_m29_badcode_and_pairtwice_only_for_dropbox(self):
        for step in ("badcode", "pairtwice"):
            self.assertIn(step, cloud_smoke.STEPS)
            with self.assertRaises(ValueError):
                cloud_smoke.make_fake(step, "drive")
        self.assertGreater(cloud_smoke.make_fake("pairtwice", "dropbox").token_delay_s, 0)
        plan = cloud_smoke.prepare("badcode", cloud_smoke.make_fake("badcode", "dropbox"), self.user)
        self.assertIn("NL_CLOUD_LINK badcode", plan.markers)
        plan = cloud_smoke.prepare("pairtwice", cloud_smoke.make_fake("pairtwice", "dropbox"), self.user)
        self.assertIn("NL_PAIR reject", plan.markers)
        plan = cloud_smoke.prepare("unlink", cloud_smoke.make_fake("unlink", "dropbox"), self.user)
        self.assertIn("NL_CLOUD_UNLINKED revoke=ok", plan.markers)

    def test_m29_authorize_link_unescapes_href(self):
        page = ('<p>x</p><a class="b" href="https://localhost:1/oauth2/authorize?client_id=k&amp;response_type=code"'
                ' target="_blank">Dropbox</a><form method="post" action="/t">')
        self.assertEqual(cloud_smoke.authorize_link(page),
                         "https://localhost:1/oauth2/authorize?client_id=k&response_type=code")
        with self.assertRaises(ValueError):
            cloud_smoke.authorize_link("<p>kein Link</p>")

    def test_m29_post_check_dropbox_reads_hash_from_zip(self):
        import save_smoke
        fake = cloud_smoke.make_fake("push", "dropbox")
        plan = cloud_smoke.prepare("push", fake, self.user)
        fake.refresh.add("x")
        fake.seed(plan.unit, cloud_smoke.save_zip(plan.unit, {"Tetris (World) (Rev 1).sav": save_smoke.BYTES_A}))
        self.assertEqual(cloud_smoke.post_check(plan, self.user, "", lambda rel: b""), [])
        plan.pair_problems.append("Kopplung: kaputt")
        self.assertIn("Kopplung: kaputt", cloud_smoke.post_check(plan, self.user, "", lambda rel: b""))

    def test_m29_leaked_secrets_checks_pair_token_and_wrong_code(self):
        fake = cloud_fake.FakeDropbox()
        self.assertEqual(cloud_smoke.leaked_secrets("NL_PAIR ok\n", fake), [])
        self.assertEqual(len(cloud_smoke.leaked_secrets("tok=" + cloud_smoke.PAIR_TOKEN, fake)), 1)
        self.assertEqual(len(cloud_smoke.leaked_secrets("code=" + cloud_smoke.WRONG_CODE, fake)), 1)

    def test_m29_pairer_runs_the_phone_flow(self):
        """Gegen eine Nachbildung der Kopplungsseite (core/pairing_http) und den echten Dropbox-Fake."""
        import http.server
        import threading
        fake = cloud_fake.FakeDropbox()
        server, port = cloud_fake.start(fake)
        posts = []

        class Pairing(http.server.BaseHTTPRequestHandler):
            consumed = False

            def log_message(self, *_):
                pass

            def _reply(self, status, text):
                body = text.encode()
                self.send_response(status)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):  # noqa: N802
                href = (f"http://127.0.0.1:{port}/oauth2/authorize?client_id=k&amp;response_type=code&amp;"
                        f"code_challenge={FakeDropboxChallenge}&amp;code_challenge_method=S256")
                self._reply(200 if self.path == "/" + cloud_smoke.PAIR_TOKEN else 404,
                            f'<a class="b" href="{href}">x</a>')

            def do_POST(self):  # noqa: N802
                n = int(self.headers.get("Content-Length") or 0)
                posts.append(self.rfile.read(n).decode())
                if Pairing.consumed:
                    self._reply(410, "gone")
                else:
                    Pairing.consumed = True
                    self._reply(200, "ok")

        pairing = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Pairing)
        threading.Thread(target=pairing.serve_forever, daemon=True).start()
        log = self.user / "dolphin.log"
        log.write_text("NL_PAIR listen 4301\n", encoding="utf-8")
        try:
            for step in ("link", "pairtwice"):
                posts.clear()
                Pairing.consumed = False
                plan = cloud_smoke.prepare(step, cloud_smoke.make_fake(step, "dropbox"), self.user)
                pairer = cloud_smoke.Pairer(plan, log, None, port=pairing.server_address[1], timeout_s=10)
                pairer.run()
                self.assertEqual(plan.pair_problems, [], step)
                self.assertEqual(len(posts), 1 if step == "link" else 2, step)
                self.assertTrue(posts[0].startswith("code=" + cloud_fake.FakeDropbox.CODE_PREFIX), step)
            # badcode: falscher Code zuerst, nach „badcode“ im Log (und rearm) der richtige
            posts.clear()
            Pairing.consumed = False
            log.write_text("NL_PAIR listen 4301\nNL_CLOUD_LINK badcode\n", encoding="utf-8")
            plan = cloud_smoke.prepare("badcode", cloud_smoke.make_fake("badcode", "dropbox"), self.user)
            pairer = cloud_smoke.Pairer(plan, log, None, port=pairing.server_address[1], timeout_s=10)
            orig = pairer._post

            def post_and_rearm(code):
                out = orig(code)
                Pairing.consumed = False  # wie rearm() nach BadCode
                return out
            pairer._post = post_and_rearm
            pairer.run()
            self.assertEqual(plan.pair_problems, [])
            self.assertEqual(posts[0], "code=" + cloud_smoke.WRONG_CODE)
            self.assertTrue(posts[1].startswith("code=" + cloud_fake.FakeDropbox.CODE_PREFIX))
        finally:
            pairing.shutdown()
            pairing.server_close()
            server.shutdown()
            server.server_close()

    def test_m29_pairer_reports_missing_listen(self):
        log = self.user / "dolphin.log"
        log.write_text("", encoding="utf-8")
        plan = cloud_smoke.prepare("link", cloud_smoke.make_fake("link", "dropbox"), self.user)
        cloud_smoke.Pairer(plan, log, None, port=9, timeout_s=0.3).run()
        self.assertEqual(len(plan.pair_problems), 1)
        self.assertIn("NL_PAIR listen", plan.pair_problems[0])


FakeDropboxChallenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"


if __name__ == "__main__":
    unittest.main()
