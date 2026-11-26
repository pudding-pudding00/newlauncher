"""Tests für tools/cloud_fake.py (M27): Google-Drive-Fake mit Gerätecode, Tokens und Dateien im Speicher."""

import io
import json
import sys
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import cloud_fake  # noqa: E402

BOUNDARY = "nl-save-7f3a9c2e5b18d604"


def save_zip(unit: str, files: dict[str, bytes]) -> tuple[bytes, str]:
    h = cloud_fake.content_hash(files)
    meta = (f"[save]\nformat = 1\nunit = {unit}\ntitle = Test\ndevice = PC\nsaved = 1700000000\n"
            f"hash = {h}\nfiles = {len(files)}\n")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr(cloud_fake.META_NAME, meta)
        for name, data in files.items():
            z.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED)
    return buf.getvalue(), h


class FakeDriveTest(unittest.TestCase):
    def setUp(self):
        self.fake = cloud_fake.FakeDrive()
        self.server, port = cloud_fake.start(self.fake)
        self.base = f"http://127.0.0.1:{port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    # --- Hilfen ---
    def call(self, method: str, path: str, body: bytes | None = None, headers: dict | None = None,
             token: str | None = None) -> tuple[int, dict, bytes]:
        req = urllib.request.Request(self.base + path, data=body, method=method, headers=dict(headers or {}))
        if token is not None:
            req.add_header("Authorization", "Bearer " + token)
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def form(self, path: str, **fields) -> tuple[int, dict]:
        status, _, body = self.call("POST", path, urllib.parse.urlencode(fields).encode(),
                                    {"Content-Type": "application/x-www-form-urlencoded"})
        return status, json.loads(body)

    def link(self) -> dict:
        _, dc = self.form("/device/code", client_id="fake-id", scope=cloud_fake.SCOPE)
        status, tok = self.form("/token", client_id="fake-id", client_secret="fake-secret",
                                device_code=dc["device_code"],
                                grant_type="urn:ietf:params:oauth:grant-type:device_code")
        self.assertEqual(status, 200)
        return tok

    def folder(self, token: str) -> str:
        q = "name='newlauncher' and mimeType='application/vnd.google-apps.folder' and 'root' in parents and trashed=false"
        _, _, body = self.call("GET", "/drive/v3/files?q=" + urllib.parse.quote(q) + "&fields=files(id)", token=token)
        files = json.loads(body)["files"]
        if files:
            return files[0]["id"]
        status, _, body = self.call("POST", "/drive/v3/files?fields=id",
                                    json.dumps({"name": "newlauncher",
                                                "mimeType": "application/vnd.google-apps.folder"}).encode(),
                                    {"Content-Type": "application/json"}, token=token)
        self.assertEqual(status, 200)
        return json.loads(body)["id"]

    def stat(self, token: str, folder: str, unit: str) -> list:
        q = f"name='{unit}.zip' and '{folder}' in parents and trashed=false"
        status, _, body = self.call("GET", "/drive/v3/files?q=" + urllib.parse.quote(q)
                                    + "&fields=files(id,version,size,appProperties)", token=token)
        self.assertEqual(status, 200)
        return json.loads(body)["files"]

    def multipart(self, meta: dict, data: bytes) -> bytes:
        return (f"--{BOUNDARY}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n{json.dumps(meta)}\r\n"
                f"--{BOUNDARY}\r\nContent-Type: application/zip\r\n\r\n").encode() + data + \
            f"\r\n--{BOUNDARY}--\r\n".encode()

    # --- Gerätecode ---
    def test_device_code_has_google_shape(self):
        status, dc = self.form("/device/code", client_id="fake-id", scope=cloud_fake.SCOPE)
        self.assertEqual(status, 200)
        for key in ("device_code", "user_code", "verification_url", "expires_in", "interval"):
            self.assertIn(key, dc)
        self.assertGreater(dc["expires_in"], 0)

    def test_device_code_needs_client_id(self):
        status, body = self.form("/device/code", scope=cloud_fake.SCOPE)
        self.assertEqual(status, 401)
        self.assertEqual(body["error"], "invalid_client")

    def test_token_pending_until_approved(self):
        self.fake.approve = "manual"
        _, dc = self.form("/device/code", client_id="fake-id", scope=cloud_fake.SCOPE)
        fields = dict(client_id="fake-id", client_secret="s", device_code=dc["device_code"],
                      grant_type="urn:ietf:params:oauth:grant-type:device_code")
        status, body = self.form("/token", **fields)
        self.assertEqual((status, body["error"]), (428, "authorization_pending"))
        self.fake.approve_all()
        status, body = self.form("/token", **fields)
        self.assertEqual(status, 200)
        self.assertTrue(body["access_token"].startswith("ya29.fake"))
        self.assertTrue(body["refresh_token"].startswith("1//fake"))

    def test_token_denied(self):
        self.fake.approve = "deny"
        _, dc = self.form("/device/code", client_id="fake-id", scope=cloud_fake.SCOPE)
        status, body = self.form("/token", client_id="fake-id", device_code=dc["device_code"],
                                 grant_type="urn:ietf:params:oauth:grant-type:device_code")
        self.assertEqual((status, body["error"]), (403, "access_denied"))

    def test_device_code_used_once(self):
        _, dc = self.form("/device/code", client_id="fake-id", scope=cloud_fake.SCOPE)
        fields = dict(client_id="fake-id", device_code=dc["device_code"],
                      grant_type="urn:ietf:params:oauth:grant-type:device_code")
        self.assertEqual(self.form("/token", **fields)[0], 200)
        self.assertEqual(self.form("/token", **fields)[1]["error"], "invalid_grant")

    # --- Konto, Ordner, Dateien ---
    def test_api_needs_bearer(self):
        self.assertEqual(self.call("GET", "/drive/v3/about?fields=user(permissionId)")[0], 401)
        self.assertEqual(self.call("GET", "/drive/v3/about?fields=user(permissionId)", token="falsch")[0], 401)

    def test_about_returns_permission_id(self):
        tok = self.link()["access_token"]
        status, _, body = self.call("GET", "/drive/v3/about?fields=user(permissionId)", token=tok)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["user"]["permissionId"], cloud_fake.ACCOUNT)

    def test_folder_is_created_once(self):
        tok = self.link()["access_token"]
        first = self.folder(tok)
        self.assertEqual(self.folder(tok), first)
        self.assertRegex(first, r"^[A-Za-z0-9_-]+$")

    def test_multipart_upload_stat_download_update(self):
        tok = self.link()["access_token"]
        folder = self.folder(tok)
        self.assertEqual(self.stat(tok, folder, "gb-x-00000000"), [])
        data, h = save_zip("gb-x-00000000", {"a.sav": b"A" * 100})
        meta = {"name": "gb-x-00000000.zip", "mimeType": "application/zip", "parents": [folder],
                "appProperties": {"hash": h, "saved": "1700000000", "device": "Wii", "unit": "gb-x-00000000"}}
        status, _, body = self.call("POST", "/upload/drive/v3/files?uploadType=multipart&fields=id,version,size,appProperties",
                                    self.multipart(meta, data),
                                    {"Content-Type": f"multipart/related; boundary={BOUNDARY}"}, token=tok)
        self.assertEqual(status, 200)
        created = json.loads(body)
        self.assertIsInstance(created["version"], str)  # echtes Drive liefert Zahlen als Strings
        self.assertEqual(created["size"], str(len(data)))
        found = self.stat(tok, folder, "gb-x-00000000")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["appProperties"]["hash"], h)
        status, _, got = self.call("GET", f"/drive/v3/files/{created['id']}?alt=media", token=tok)
        self.assertEqual((status, got), (200, data))

        data2, h2 = save_zip("gb-x-00000000", {"a.sav": b"B" * 100})
        meta2 = {"name": "gb-x-00000000.zip", "mimeType": "application/zip",
                 "appProperties": {"hash": h2, "saved": "1700000100", "device": "Wii", "unit": "gb-x-00000000"}}
        status, _, body = self.call("PATCH", f"/upload/drive/v3/files/{created['id']}?uploadType=multipart&fields=id",
                                    self.multipart(meta2, data2),
                                    {"Content-Type": f"multipart/related; boundary={BOUNDARY}"}, token=tok)
        self.assertEqual(status, 200)
        self.assertGreater(int(json.loads(body)["version"]), int(created["version"]))
        self.assertEqual(self.fake.file_bytes("gb-x-00000000"), data2)

    def test_resumable_upload(self):
        tok = self.link()["access_token"]
        folder = self.folder(tok)
        data, h = save_zip("wii-RMGP", {"big.bin": bytes(range(256)) * 100})
        meta = {"name": "wii-RMGP.zip", "mimeType": "application/zip", "parents": [folder],
                "appProperties": {"hash": h, "saved": "1", "device": "Wii", "unit": "wii-RMGP"}}
        status, headers, _ = self.call("POST", "/upload/drive/v3/files?uploadType=resumable&fields=id,version",
                                       json.dumps(meta).encode(),
                                       {"Content-Type": "application/json", "X-Upload-Content-Type": "application/zip"},
                                       token=tok)
        self.assertEqual(status, 200)
        location = headers["Location"]
        self.assertTrue(location.startswith(self.base + "/"))
        status, _, body = self.call("PUT", location[len(self.base):], data, {"Content-Type": "application/zip"},
                                    token=tok)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["size"], str(len(data)))
        self.assertEqual(self.fake.file_bytes("wii-RMGP"), data)

    def test_unknown_file_is_404(self):
        tok = self.link()["access_token"]
        self.assertEqual(self.call("GET", "/drive/v3/files/nichtda?alt=media", token=tok)[0], 404)

    # --- Ablauf und Refresh ---
    def test_expire_access_once_then_refresh(self):
        self.fake.expire_access_once = True
        tok = self.link()
        about = "/drive/v3/about?fields=user(permissionId)"
        self.assertEqual(self.call("GET", about, token=tok["access_token"])[0], 200)  # Anmeldung klappt noch
        path = "/drive/v3/files?q=" + urllib.parse.quote("name='x.zip' and 'p' in parents and trashed=false")
        self.assertEqual(self.call("GET", path, token=tok["access_token"])[0], 401)
        status, fresh = self.form("/token", client_id="fake-id", client_secret="s",
                                  refresh_token=tok["refresh_token"], grant_type="refresh_token")
        self.assertEqual(status, 200)
        self.assertNotEqual(fresh["access_token"], tok["access_token"])
        self.assertNotIn("refresh_token", fresh)  # Google schickt beim Refresh keinen neuen
        self.assertEqual(self.call("GET", path, token=fresh["access_token"])[0], 200)
        self.assertEqual(self.fake.refreshes, 1)

    def test_wrong_refresh_token_is_invalid_grant(self):
        self.link()
        status, body = self.form("/token", client_id="fake-id", refresh_token="1//falsch", grant_type="refresh_token")
        self.assertEqual((status, body["error"]), (400, "invalid_grant"))

    def test_offline_api_drops_connection(self):
        tok = self.link()["access_token"]
        self.fake.offline_api = True
        with self.assertRaises((urllib.error.URLError, ConnectionError, OSError)):
            req = urllib.request.Request(self.base + "/drive/v3/about", headers={"Authorization": "Bearer " + tok})
            urllib.request.urlopen(req, timeout=5).read()

    def test_offline_after_link_closes_listener_after_about(self):
        self.fake.offline_after_link = True
        tok = self.link()["access_token"]
        self.assertEqual(self.call("GET", "/drive/v3/about?fields=user(permissionId)", token=tok)[0], 200)
        deadline = time.monotonic() + 5
        refused = False
        while not refused and time.monotonic() < deadline:  # das Schließen läuft in einem eigenen Thread
            try:
                req = urllib.request.Request(self.base + "/drive/v3/files?q=x",
                                             headers={"Authorization": "Bearer " + tok})
                urllib.request.urlopen(req, timeout=2).read()
                time.sleep(0.05)
            except urllib.error.HTTPError:
                time.sleep(0.05)
            except (urllib.error.URLError, ConnectionError, OSError):
                refused = True
        self.assertTrue(refused)

    # --- Seed und Dump ---
    def test_seed_is_visible_with_hash_from_zip(self):
        data, h = save_zip("gb-tetris-1234abcd", {"t.sav": b"C" * 10})
        self.fake.seed("gb-tetris-1234abcd", data, device="Dolphin", saved=1700000000)
        tok = self.link()["access_token"]
        found = self.stat(tok, self.folder(tok), "gb-tetris-1234abcd")
        self.assertEqual(found[0]["appProperties"], {"hash": h, "saved": "1700000000", "device": "Dolphin",
                                                     "unit": "gb-tetris-1234abcd"})

    def test_state_dump_has_no_tokens(self):
        tok = self.link()
        data, h = save_zip("u1", {"x": b"1"})
        self.fake.seed("u1", data)
        dump = json.dumps(self.fake.state_dump())
        self.assertNotIn(tok["access_token"], dump)
        self.assertNotIn(tok["refresh_token"], dump)
        self.assertIn(h, dump)
        self.assertEqual(self.fake.state_dump()["files"]["u1.zip"]["sha256"], cloud_fake.sha256_hex(data))

    def test_secrets_lists_every_issued_token(self):
        tok = self.link()
        self.assertIn(tok["access_token"], self.fake.secrets())
        self.assertIn(tok["refresh_token"], self.fake.secrets())


class FakeDriveRevokeTest(unittest.TestCase):
    def test_revoke_counts_and_unlinks(self):
        fake = cloud_fake.FakeDrive()
        server, port = cloud_fake.start(fake)
        try:
            base = f"http://127.0.0.1:{port}"

            def post(path, **fields):
                req = urllib.request.Request(base + path, data=urllib.parse.urlencode(fields).encode(), method="POST")
                try:
                    with urllib.request.urlopen(req, timeout=5) as r:
                        return r.status, json.loads(r.read())
                except urllib.error.HTTPError as e:
                    return e.code, json.loads(e.read())

            _, dc = post("/device/code", client_id="x")
            _, tok = post("/token", client_id="x", device_code=dc["device_code"], grant_type=cloud_fake.DEVICE_GRANT)
            self.assertEqual(post("/revoke", token="falsch")[0], 400)
            self.assertEqual(post("/revoke", token=tok["refresh_token"])[0], 200)
            dump = fake.state_dump()
            self.assertEqual(dump["revokes"], 1)
            self.assertFalse(dump["linked"])
        finally:
            server.shutdown()
            server.server_close()


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class FakeOneDriveTest(unittest.TestCase):
    API = "/v1.0/me/drive"

    def setUp(self):
        self.fake = cloud_fake.FakeOneDrive()
        self.server, port = cloud_fake.start(self.fake)
        self.base = f"http://127.0.0.1:{port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def call(self, method, path, body=None, headers=None, token=None):
        url = path if path.startswith("http") else self.base + path
        req = urllib.request.Request(url, data=body, method=method, headers=dict(headers or {}))
        if token is not None:
            req.add_header("Authorization", "Bearer " + token)
        opener = urllib.request.build_opener(_NoRedirect())
        try:
            with opener.open(req, timeout=5) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def form(self, path, **fields):
        status, _, body = self.call("POST", path, urllib.parse.urlencode(fields).encode(),
                                    {"Content-Type": "application/x-www-form-urlencoded"})
        return status, json.loads(body)

    def link(self) -> str:
        status, dc = self.form("/consumers/oauth2/v2.0/devicecode", client_id="cid", scope=cloud_fake.MS_SCOPE)
        self.assertEqual(status, 200)
        self.assertIn("verification_uri", dc)
        status, tok = self.form("/consumers/oauth2/v2.0/token", client_id="cid", device_code=dc["device_code"],
                                grant_type=cloud_fake.DEVICE_GRANT)
        self.assertEqual(status, 200)
        self.assertIn("refresh_token", tok)
        return tok["access_token"]

    def item_path(self, unit):
        return f"{self.API}/special/approot:/{unit}.zip"

    def test_pending_is_400_authorization_pending(self):
        fake = cloud_fake.FakeOneDrive(approve="manual")
        _, dc = fake.device_code({"client_id": "cid"})
        status, obj = fake.token({"client_id": "cid", "device_code": dc["device_code"],
                                  "grant_type": cloud_fake.DEVICE_GRANT})
        self.assertEqual((status, obj["error"]), (400, "authorization_pending"))

    def test_roundtrip_put_stat_patch_download(self):
        tok = self.link()
        status, _, body = self.call("GET", self.API + "?$select=id", token=tok)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["id"], cloud_fake.OD_DRIVE_ID)
        self.assertEqual(self.call("GET", self.item_path("gb-x-1") + "?$select=id,cTag", token=tok)[0], 404)
        data, h = save_zip("gb-x-1", {"a.sav": b"A"})
        status, _, body = self.call("PUT", self.item_path("gb-x-1") + ":/content?@microsoft.graph.conflictBehavior=fail",
                                    data, {"Content-Type": "application/zip"}, token=tok)
        self.assertEqual(status, 201)
        item = json.loads(body)
        self.assertTrue(item["cTag"].startswith('"c:'))
        desc = f"nl1 hash={h} saved=1700000000 device=PC"
        status, _, _ = self.call("PATCH", f"{self.API}/items/{item['id']}", json.dumps({"description": desc}).encode(),
                                 {"Content-Type": "application/json"}, token=tok)
        self.assertEqual(status, 200)
        status, _, body = self.call("GET", self.item_path("gb-x-1") + "?$select=id,cTag,size,description", token=tok)
        self.assertEqual(json.loads(body)["description"], desc)
        status, headers, _ = self.call("GET", f"{self.API}/items/{item['id']}/content", token=tok)
        self.assertEqual(status, 302)
        self.assertEqual(self.call("GET", headers["Location"])[2], data)
        self.assertEqual(self.fake.state_dump()["files"]["gb-x-1.zip"]["description"], desc)

    def test_wrong_if_match_is_412_and_existing_fail_is_409(self):
        tok = self.link()
        data, _ = save_zip("u", {"a": b"1"})
        _, _, body = self.call("PUT", self.item_path("u") + ":/content", data, token=tok)
        ctag = json.loads(body)["cTag"]
        self.assertEqual(self.call("PUT", self.item_path("u") + ":/content", data, {"If-Match": '"c:{x},9"'},
                                   token=tok)[0], 412)
        self.assertEqual(self.call("PUT", self.item_path("u") + ":/content?@microsoft.graph.conflictBehavior=fail",
                                   data, token=tok)[0], 409)
        status, _, body = self.call("PUT", self.item_path("u") + ":/content", data, {"If-Match": ctag}, token=tok)
        self.assertEqual(status, 200)
        self.assertNotEqual(json.loads(body)["cTag"], ctag)

    def test_redirect_target_rejects_bearer(self):
        tok = self.link()
        data, _ = save_zip("u", {"a": b"1"})
        _, _, body = self.call("PUT", self.item_path("u") + ":/content", data, token=tok)
        _, headers, _ = self.call("GET", f"{self.API}/items/{json.loads(body)['id']}/content", token=tok)
        self.assertEqual(self.call("GET", headers["Location"], token=tok)[0], 400)

    def test_upload_session_put_without_bearer(self):
        tok = self.link()
        data, _ = save_zip("big", {"a": b"x" * 1000})
        status, _, body = self.call("POST", self.item_path("big") + ":/createUploadSession",
                                    json.dumps({"item": {"@microsoft.graph.conflictBehavior": "fail"}}).encode(),
                                    {"Content-Type": "application/json"}, token=tok)
        self.assertEqual(status, 200)
        url = json.loads(body)["uploadUrl"]
        rng = {"Content-Range": f"bytes 0-{len(data) - 1}/{len(data)}"}
        self.assertEqual(self.call("PUT", url, data, rng, token=tok)[0], 400)
        status, _, body = self.call("PUT", url, data, rng)
        self.assertEqual(status, 201)
        self.assertEqual(self.fake.file_bytes("big"), data)

    def test_api_needs_bearer_and_dump_has_no_tokens(self):
        self.assertEqual(self.call("GET", self.API)[0], 401)
        tok = self.link()
        self.assertNotIn(tok, json.dumps(self.fake.state_dump()))
        self.assertIn(tok, self.fake.secrets())

    def test_seed_writes_description(self):
        data, h = save_zip("wii-RMGP", {"smoke.sav": b"B"})
        self.fake.seed("wii-RMGP", data, device="Dolphin PC", saved=5)
        desc = self.fake.state_dump()["files"]["wii-RMGP.zip"]["description"]
        self.assertEqual(desc, f"nl1 hash={h} saved=5 device=Dolphin%20PC")


class CliTest(unittest.TestCase):
    def test_parse_seed(self):
        self.assertEqual(cloud_fake.parse_seed("gb-x-1=C:/a/b.zip"), ("gb-x-1", Path("C:/a/b.zip")))
        with self.assertRaises(ValueError):
            cloud_fake.parse_seed("ohne-gleich")
        with self.assertRaises(ValueError):
            cloud_fake.parse_seed("bad/unit=x.zip")


if __name__ == "__main__":
    unittest.main()
