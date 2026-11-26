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
        self.assertEqual(cloud_smoke.PROVIDERS, ("drive", "onedrive"))
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


if __name__ == "__main__":
    unittest.main()
