"""Cloud-Fakes für die Smokes (M27 Google Drive, M28 OneDrive, M29 Dropbox): Gerätecode, Tokens und Dateien im Speicher, optional
über TLS.

Bildet genau die Anfragen nach, die src/core/oauth_device.cpp und src/core/drive_provider.cpp senden:
  POST /device/code, POST /token (device_code, refresh_token)
  GET  /drive/v3/about, GET/POST /drive/v3/files, GET /drive/v3/files/<id>?alt=media
  POST /upload/drive/v3/files, PATCH /upload/drive/v3/files/<id> (multipart oder resumable + PUT)
  POST /revoke (token=…)
--provider onedrive bildet src/core/onedrive_provider.cpp nach (siehe FakeOneDrive),
--provider dropbox src/core/dropbox_provider.cpp mit PKCE-Freigabeseite (siehe FakeDropbox).

Aufruf: python tools/cloud_fake.py [--provider drive|onedrive|dropbox] [--port N] [--cert server.pem --key server.key] [--approve auto|manual|deny]
        [--expire-access-once] [--offline-api] [--offline-after-link] [--seed <einheit>=<zip>] [--state-dump datei.json]

Tokens sind Fantasiewerte (ya29.fake…, 1//fake…); der Zustands-Dump enthält keine Tokens.
"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import io
import json
import re
import secrets as _secrets
import ssl
import sys
import threading
import time
import urllib.parse
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SCOPE = "https://www.googleapis.com/auth/drive.file"
ACCOUNT = "fakeperm01"
META_NAME = "newlauncher-save.ini"
FOLDER_MIME = "application/vnd.google-apps.folder"
DEVICE_GRANT = "urn:ietf:params:oauth:grant-type:device_code"
_SAFE_UNIT = re.compile(r"^[A-Za-z0-9_-]+$")
_MAX_BODY = 32 << 20


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def content_hash(files: dict[str, bytes]) -> str:
    """Inhalts-Hash wie core/save_archive: SHA-256 über sortierte Zeilen „pfad\\ngröße\\nsha256hex\\n“."""
    h = hashlib.sha256()
    for path in sorted(files):
        data = files[path]
        h.update(f"{path}\n{len(data)}\n{sha256_hex(data)}\n".encode())
    return h.hexdigest()


def zip_meta(data: bytes) -> dict[str, str]:
    """[save]-Werte aus newlauncher-save.ini im ZIP."""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        text = z.read(META_NAME).decode()
    return dict(line.split(" = ", 1) for line in text.splitlines() if " = " in line)


def parse_seed(spec: str) -> tuple[str, Path]:
    unit, sep, path = spec.partition("=")
    if not sep or not path or not _SAFE_UNIT.match(unit):
        raise ValueError(f"--seed erwartet <einheit>=<zip>: {spec}")
    return unit, Path(path)


@dataclass
class DriveFile:
    id: str
    name: str
    parent: str
    mime: str
    data: bytes = b""
    version: int = 1
    props: dict = field(default_factory=dict)

    def resource(self) -> dict:
        out = {"id": self.id, "version": str(self.version), "size": str(len(self.data))}
        if self.props:
            out["appProperties"] = dict(self.props)
        return out


class FakeDrive:
    """Zustand des Fakes; alle Methoden sind threadsicher (ThreadingHTTPServer)."""

    DEVICE_PATH, TOKEN_PATH, REVOKE_PATH = "/device/code", "/token", "/revoke"
    ACCOUNT_PATH = "/drive/v3/about"  # gehört zur Anmeldung: expire_access_once lässt sie durch
    PENDING_STATUS = 428
    SCOPE = SCOPE
    ACCESS_PREFIX, REFRESH_PREFIX, DEVICE_PREFIX = "ya29.fake", "1//fake", "AH-fakedev"

    def __init__(self, approve: str = "auto", expire_access_once: bool = False, offline_api: bool = False,
                 offline_after_link: bool = False):
        self.approve = approve
        self.expire_access_once = expire_access_once  # erste Datei-Anfrage nach der Anmeldung → 401
        self.offline_api = offline_api
        self.offline_after_link = offline_after_link  # nach dem ersten about: Listener zu (Verbindung abgewiesen)
        self.on_offline = None                        # setzt start(): schließt den Listener
        self.lock = threading.Lock()
        self.files: dict[str, DriveFile] = {}
        self.devices: dict[str, str] = {}       # device_code → pending|approved|denied|used
        self.access: set[str] = set()
        self.refresh: set[str] = set()
        self.issued: list[str] = []             # alle je ausgegebenen Tokens und Codes (für den Log-grep)
        self.sessions: dict[str, tuple[str | None, dict]] = {}  # resumable: id → (Datei-ID oder None, Metadaten)
        self.refreshes = 0
        self.revokes = 0
        self.uploads = 0
        self.requests: list[str] = []           # "METHODE pfad" ohne Abfrage
        self._expired_once = False
        self._seq = 0
        self.dump_path: Path | None = None

    # --- Hilfen ---
    def _id(self, prefix: str) -> str:
        self._seq += 1
        return f"{prefix}{self._seq:04d}{_secrets.token_hex(4)}"

    def _issue(self, prefix: str) -> str:
        tok = prefix + _secrets.token_urlsafe(18)
        self.issued.append(tok)
        return tok

    def _folder(self) -> DriveFile | None:
        return next((f for f in self.files.values() if f.mime == FOLDER_MIME and f.name == "newlauncher"), None)

    def _by_name(self, name: str, parent: str | None = None) -> DriveFile | None:
        return next((f for f in self.files.values()
                     if f.name == name and f.mime != FOLDER_MIME and (parent is None or f.parent == parent)), None)

    def _dump(self) -> None:
        if self.dump_path is not None:
            self.dump_path.write_text(json.dumps(self.state_dump(), indent=1), encoding="utf-8")

    # --- öffentliche Schnittstelle für Tests und Smoke ---
    def approve_all(self) -> None:
        with self.lock:
            for k, v in self.devices.items():
                if v == "pending":
                    self.devices[k] = "approved"

    def ensure_folder(self) -> DriveFile:
        folder = self._folder()
        if folder is None:
            folder = DriveFile(self._id("fld"), "newlauncher", "root", FOLDER_MIME)
            self.files[folder.id] = folder
        return folder

    def seed(self, unit: str, data: bytes, device: str = "Dolphin", saved: int | None = None) -> DriveFile:
        meta = zip_meta(data)
        with self.lock:
            folder = self.ensure_folder()
            props = {"hash": meta["hash"], "saved": str(saved if saved is not None else int(meta.get("saved", "0"))),
                     "device": device, "unit": unit}
            f = DriveFile(self._id("fil"), unit + ".zip", folder.id, "application/zip", data, 1, props)
            self.files[f.id] = f
            self._dump()
            return f

    def file_bytes(self, unit: str) -> bytes | None:
        with self.lock:
            f = self._by_name(unit + ".zip")
            return f.data if f else None

    def state_dump(self) -> dict:
        files = {}
        for f in self.files.values():
            if f.mime == FOLDER_MIME:
                continue
            files[f.name] = {"version": f.version, "size": len(f.data), "sha256": sha256_hex(f.data),
                             "appProperties": dict(f.props)}
        return {"account": ACCOUNT, "files": files, "uploads": self.uploads, "refreshes": self.refreshes,
                "linked": bool(self.refresh), "revokes": self.revokes}

    def secrets(self) -> list[str]:
        with self.lock:
            return list(self.issued)

    # --- OAuth ---
    def _verify(self) -> dict:
        return {"verification_url": "https://www.google.com/device"}

    def device_code(self, form: dict) -> tuple[int, dict]:
        if not form.get("client_id"):
            return 401, {"error": "invalid_client"}
        with self.lock:
            code = self._issue(self.DEVICE_PREFIX)
            self.devices[code] = {"auto": "approved", "deny": "denied"}.get(self.approve, "pending")
        return 200, {"device_code": code, "user_code": "FAKE-CODE", **self._verify(), "expires_in": 1800, "interval": 1}

    def token(self, form: dict) -> tuple[int, dict]:
        if not form.get("client_id"):
            return 401, {"error": "invalid_client"}
        grant = form.get("grant_type")
        with self.lock:
            if grant == DEVICE_GRANT:
                state = self.devices.get(form.get("device_code", ""))
                if state is None or state == "used":
                    return 400, {"error": "invalid_grant"}
                if state == "pending":
                    return self.PENDING_STATUS, {"error": "authorization_pending"}
                if state == "denied":
                    return 403, {"error": "access_denied"}
                self.devices[form["device_code"]] = "used"
                access, refresh = self._issue(self.ACCESS_PREFIX), self._issue(self.REFRESH_PREFIX)
                self.access.add(access)
                self.refresh.add(refresh)
                return 200, {"access_token": access, "refresh_token": refresh, "expires_in": 3599,
                             "scope": self.SCOPE, "token_type": "Bearer"}
            if grant == "refresh_token":
                if form.get("refresh_token") not in self.refresh:
                    return 400, {"error": "invalid_grant"}
                access = self._issue(self.ACCESS_PREFIX)
                self.access.add(access)
                self.refreshes += 1
                self._dump()
                return 200, {"access_token": access, "expires_in": 3599, "scope": self.SCOPE, "token_type": "Bearer"}
        return 400, {"error": "unsupported_grant_type"}

    def revoke(self, form: dict) -> tuple[int, dict]:
        """Wie Googles /revoke: Refresh-Token ungültig, mit ihm alle Zugriffs-Tokens."""
        with self.lock:
            if form.get("token") not in self.refresh:
                return 400, {"error": "invalid_token"}
            self.refresh.discard(form["token"])
            self.access.clear()
            self.revokes += 1
            self._dump()
            return 200, {}

    def authorized(self, header: str | None, path: str = "") -> bool:
        if not header or not header.startswith("Bearer "):
            return False
        tok = header[len("Bearer "):]
        with self.lock:
            if tok not in self.access:
                return False
            # about gehört zur Anmeldung; abgelaufen wird erst der Token für Datei-Anfragen
            if self.expire_access_once and not self._expired_once and path != self.ACCOUNT_PATH:
                self._expired_once = True
                self.access.discard(tok)  # wie ein abgelaufener Token: ab jetzt immer 401
                return False
            return True

    # --- Drive ---
    def list_files(self, q: str) -> tuple[int, dict]:
        with self.lock:
            if f"mimeType='{FOLDER_MIME}'" in q:
                m = re.search(r"name='([^']*)'", q)
                folder = self._folder()
                hit = folder is not None and m is not None and m.group(1) == folder.name and "'root' in parents" in q
                return 200, {"files": [{"id": folder.id}] if hit else []}
            m = re.fullmatch(r"name='([^']+)' and '([^']+)' in parents and trashed=false", q)
            if m is None:
                return 400, {"error": {"code": 400, "message": "unbekannte Abfrage"}}
            name, parent = m.groups()
            return 200, {"files": [f.resource() for f in self.files.values()
                                   if f.name == name and f.parent == parent and f.mime != FOLDER_MIME]}

    def create_folder(self, meta: dict) -> tuple[int, dict]:
        if meta.get("mimeType") != FOLDER_MIME or meta.get("name") != "newlauncher":
            return 400, {"error": {"code": 400, "message": "nur der Ordner newlauncher"}}
        with self.lock:
            return 200, {"id": self.ensure_folder().id}

    def media(self, file_id: str) -> bytes | None:
        with self.lock:
            f = self.files.get(file_id)
            return f.data if f is not None and f.mime != FOLDER_MIME else None

    def write(self, file_id: str | None, meta: dict, data: bytes) -> tuple[int, dict]:
        """Anlegen (file_id None, parents nötig) oder neue Version; liefert die Datei-Ressource."""
        with self.lock:
            if file_id is None:
                parents = meta.get("parents") or []
                if len(parents) != 1 or parents[0] not in self.files or not meta.get("name"):
                    return 400, {"error": {"code": 400, "message": "parents fehlt"}}
                f = DriveFile(self._id("fil"), meta["name"], parents[0], meta.get("mimeType", "application/zip"))
                self.files[f.id] = f
                f.version = 0
            else:
                f = self.files.get(file_id)
                if f is None or f.mime == FOLDER_MIME:
                    return 404, {"error": {"code": 404, "message": "File not found"}}
                if "parents" in meta:
                    return 403, {"error": {"code": 403, "message": "parents beim Update nicht erlaubt"}}
            f.data = data
            f.version += 1
            if "appProperties" in meta:
                f.props.update({k: str(v) for k, v in meta["appProperties"].items()})
            self.uploads += 1
            self._dump()
            return 200, f.resource()

    def start_session(self, file_id: str | None, meta: dict) -> str:
        with self.lock:
            sid = self._id("ses")
            self.sessions[sid] = (file_id, meta)
            return sid

    def finish_session(self, sid: str, data: bytes) -> tuple[int, dict]:
        with self.lock:
            entry = self.sessions.pop(sid, None)
        if entry is None:
            return 404, {"error": {"code": 404, "message": "Sitzung unbekannt"}}
        return self.write(entry[0], entry[1], data)


def parse_multipart(content_type: str, body: bytes) -> tuple[dict, bytes] | None:
    m = re.search(r"boundary=([^;\s]+)", content_type or "")
    if m is None:
        return None
    sep = b"--" + m.group(1).strip('"').encode()
    parts = body.split(sep)
    # ["", meta-teil, daten-teil, "--\r\n"]
    if len(parts) < 4 or not parts[-1].startswith(b"--"):
        return None
    out = []
    for part in parts[1:3]:
        head, _, content = part.partition(b"\r\n\r\n")
        if not content.endswith(b"\r\n"):
            return None
        out.append(content[:-2])
    try:
        return json.loads(out[0]), out[1]
    except ValueError:
        return None


_HEADER_NAME_RE = re.compile(r"^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$")


def _validate_header(name: str, value: str) -> tuple[str, str]:
    """Validate HTTP response header name and value to prevent CRLF/response splitting."""
    if not isinstance(name, str) or not _HEADER_NAME_RE.match(name):
        raise ValueError(f"invalid HTTP header name: {name!r}")
    sval = str(value)
    for ch in sval:
        code = ord(ch)
        if (code < 0x20 and ch != "\t") or code == 0x7F:
            raise ValueError(f"invalid control character in HTTP header {name!r}")
    return name, sval


class Handler(http.server.BaseHTTPRequestHandler):
    fake: FakeDrive  # wird von start() gesetzt
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):  # leise
        pass

    def _send(self, status: int, body: bytes, ctype: str = "application/json", headers: dict | None = None) -> None:
        _, safe_ctype = _validate_header("Content-Type", ctype)
        validated = [_validate_header(k, v) for k, v in (headers or {}).items()]
        self.send_response(status)
        self.send_header("Content-Type", safe_ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in validated:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, status: int, obj: dict, headers: dict | None = None) -> None:
        self._send(status, json.dumps(obj).encode(), headers=headers)

    def _body(self) -> bytes:
        n = int(self.headers.get("Content-Length") or 0)
        if n > _MAX_BODY:
            raise ValueError("zu groß")
        return self.rfile.read(n) if n else b""

    def _base(self) -> str:
        scheme = "https" if isinstance(self.connection, ssl.SSLSocket) else "http"
        return f"{scheme}://{self.headers.get('Host')}"

    def _oauth(self, method: str, path: str, body: bytes) -> bool:
        fake = self.fake
        handlers = {fake.DEVICE_PATH: fake.device_code, fake.TOKEN_PATH: fake.token}
        if not isinstance(fake, FakeOneDrive):
            handlers[fake.REVOKE_PATH] = fake.revoke  # Microsoft hat keinen Widerruf
        if method != "POST" or path not in handlers:
            return False
        form = dict(urllib.parse.parse_qsl(body.decode("utf-8", "replace")))
        self._json(*handlers[path](form))
        return True

    def _route(self, method: str) -> None:
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query, keep_blank_values=True))
        path = url.path
        fake = self.fake
        with fake.lock:
            fake.requests.append(f"{method} {path}")
        try:
            body = self._body()
        except ValueError:
            self._json(413, {"error": "too_large"})
            return

        if self._oauth(method, path, body):
            return

        if not (path.startswith("/drive/") or path.startswith("/upload/")):
            self._json(404, {"error": "not_found"})
            return
        if fake.offline_api:
            self.close_connection = True
            self.connection.close()  # wie ein Netz ohne Antwort
            return
        if not fake.authorized(self.headers.get("Authorization"), path):
            self._json(401, {"error": {"code": 401, "message": "Invalid Credentials"}})
            return

        if method == "GET" and path == "/drive/v3/about":
            if fake.offline_after_link and fake.on_offline is not None:
                # Erst den Listener schließen, dann antworten: Jede spätere Verbindung wird abgewiesen
                # (wie ein Netz ohne Server), auch wenn der Client sofort neu verbindet.
                fake.offline_after_link = False
                fake.on_offline()
                self.close_connection = True
            self._json(200, {"user": {"permissionId": ACCOUNT}})
        elif method == "GET" and path == "/drive/v3/files":
            self._json(*fake.list_files(query.get("q", "")))
        elif method == "POST" and path == "/drive/v3/files":
            try:
                meta = json.loads(body)
            except ValueError:
                self._json(400, {"error": {"code": 400, "message": "json"}})
                return
            self._json(*fake.create_folder(meta))
        elif method == "GET" and path.startswith("/drive/v3/files/") and query.get("alt") == "media":
            data = fake.media(path.rsplit("/", 1)[1])
            if data is None:
                self._json(404, {"error": {"code": 404, "message": "File not found"}})
            else:
                self._send(200, data, "application/zip")
        elif method in ("POST", "PATCH") and (path == "/upload/drive/v3/files"
                                              or path.startswith("/upload/drive/v3/files/")):
            file_id = None if path == "/upload/drive/v3/files" else path.rsplit("/", 1)[1]
            if method == "PATCH" and file_id is None or method == "POST" and file_id is not None:
                self._json(405, {"error": "method"})
                return
            kind = query.get("uploadType")
            if kind == "multipart":
                parsed = parse_multipart(self.headers.get("Content-Type", ""), body)
                if parsed is None:
                    self._json(400, {"error": {"code": 400, "message": "multipart"}})
                    return
                self._json(*fake.write(file_id, *parsed))
            elif kind == "resumable":
                try:
                    meta = json.loads(body or b"{}")
                except ValueError:
                    self._json(400, {"error": {"code": 400, "message": "json"}})
                    return
                sid = fake.start_session(file_id, meta)
                self._json(200, {}, {"Location": f"{self._base()}/upload/drive/v3/sessions/{sid}"})
            else:
                self._json(400, {"error": {"code": 400, "message": "uploadType"}})
        elif method == "PUT" and path.startswith("/upload/drive/v3/sessions/"):
            self._json(*fake.finish_session(path.rsplit("/", 1)[1], body))
        else:
            self._json(404, {"error": "not_found"})

    def do_GET(self):  # noqa: N802
        self._route("GET")

    def do_POST(self):  # noqa: N802
        self._route("POST")

    def do_PATCH(self):  # noqa: N802
        self._route("PATCH")

    def do_PUT(self):  # noqa: N802
        self._route("PUT")


# --- OneDrive (M28) ---------------------------------------------------------------------------------------------------

MS_SCOPE = "Files.ReadWrite.AppFolder offline_access"
OD_DRIVE_ID = "fakeod0001"
OD_API = "/v1.0/me/drive"
OD_APPROOT = OD_API + "/special/approot:/"
_OD_GUID = "{8E2B4C1A-0F3D-4A7B-9C5E-1D2F3A4B5C6D}"


@dataclass
class OneDriveItem:
    id: str
    name: str
    data: bytes = b""
    ctr: int = 1
    description: str = ""

    def ctag(self) -> str:
        return f'"c:{_OD_GUID},{self.ctr}"'

    def resource(self) -> dict:
        out = {"id": self.id, "name": self.name, "cTag": self.ctag(), "eTag": f'"{_OD_GUID},{self.ctr}"',
               "size": len(self.data)}
        if self.description:
            out["description"] = self.description
        return out


def onedrive_description(h: str, saved: int, device: str) -> str:
    """Wie oneDriveDescription in core/onedrive_provider: nl1 hash=… saved=… device=<prozentkodiert>."""
    return f"nl1 hash={h} saved={saved} device={urllib.parse.quote(device, safe='')}"


def parse_onedrive_description(text: str) -> dict[str, str]:
    if not text.startswith("nl1 "):
        return {}
    out = dict(part.split("=", 1) for part in text[4:].split(" ") if "=" in part)
    if "device" in out:
        out["device"] = urllib.parse.unquote(out["device"])
    return out


class FakeOneDrive(FakeDrive):
    """Microsoft-Graph-Fake: Gerätecode unter /consumers, App-Ordner approot, cTag-Zähler, 412/409, 302-Download auf
    /dl/<id> (lehnt einen Bearer-Header ab wie eine vorab signierte URL) und Upload-Sitzungen ohne Bearer."""

    DEVICE_PATH = "/consumers/oauth2/v2.0/devicecode"
    TOKEN_PATH = "/consumers/oauth2/v2.0/token"
    ACCOUNT_PATH = OD_API
    PENDING_STATUS = 400
    SCOPE = MS_SCOPE
    ACCESS_PREFIX, REFRESH_PREFIX, DEVICE_PREFIX = "EwB-fake", "M.C5-fake", "DAQAB-fakedev"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.items: dict[str, OneDriveItem] = {}
        self.od_sessions: dict[str, tuple[str, str | None, str]] = {}  # id → (Name, If-Match, conflictBehavior)

    def _verify(self) -> dict:
        return {"verification_uri": "https://www.microsoft.com/link", "message": "fake"}

    def _item(self, name: str) -> OneDriveItem | None:
        return next((i for i in self.items.values() if i.name == name), None)

    def seed(self, unit: str, data: bytes, device: str = "Dolphin", saved: int | None = None) -> OneDriveItem:
        meta = zip_meta(data)
        with self.lock:
            item = OneDriveItem(self._id("FAKE!"), unit + ".zip", data)
            item.description = onedrive_description(meta["hash"], saved if saved is not None
                                                    else int(meta.get("saved", "0")), device)
            self.items[item.id] = item
            self._dump()
            return item

    def file_bytes(self, unit: str) -> bytes | None:
        with self.lock:
            item = self._item(unit + ".zip")
            return item.data if item else None

    def state_dump(self) -> dict:
        files = {i.name: {"cTag": i.ctag(), "size": len(i.data), "sha256": sha256_hex(i.data),
                          "description": i.description} for i in self.items.values()}
        return {"account": OD_DRIVE_ID, "files": files, "uploads": self.uploads, "refreshes": self.refreshes,
                "linked": bool(self.refresh), "revokes": self.revokes}

    def stat(self, name: str) -> tuple[int, dict]:
        with self.lock:
            item = self._item(name)
            if item is None:
                return 404, {"error": {"code": "itemNotFound", "message": "Item does not exist"}}
            return 200, item.resource()

    def _precondition(self, item: OneDriveItem | None, if_match: str | None, behavior: str) -> tuple[int, dict] | None:
        if if_match is not None and (item is None or if_match != item.ctag()):
            return 412, {"error": {"code": "preconditionFailed", "message": "ETag does not match"}}
        if behavior == "fail" and item is not None:
            return 409, {"error": {"code": "nameAlreadyExists", "message": "Name already exists"}}
        return None

    def put_content(self, name: str, data: bytes, if_match: str | None, behavior: str) -> tuple[int, dict]:
        with self.lock:
            item = self._item(name)
            bad = self._precondition(item, if_match, behavior)
            if bad is not None:
                return bad
            created = item is None
            if created:
                item = OneDriveItem(self._id("FAKE!"), name, data)
                self.items[item.id] = item
            else:
                item.data = data
                item.ctr += 1
            self.uploads += 1
            self._dump()
            return (201 if created else 200), item.resource()

    def set_description(self, item_id: str, description: str) -> tuple[int, dict]:
        with self.lock:
            item = self.items.get(item_id)
            if item is None:
                return 404, {"error": {"code": "itemNotFound", "message": "Item does not exist"}}
            item.description = description
            self._dump()
            return 200, item.resource()

    def content(self, item_id: str) -> bytes | None:
        with self.lock:
            item = self.items.get(item_id)
            return item.data if item is not None else None

    def start_upload(self, name: str, if_match: str | None, behavior: str) -> tuple[int, dict] | str:
        with self.lock:
            bad = self._precondition(self._item(name), if_match, behavior)
            if bad is not None:
                return bad
            sid = self._id("ups")
            self.od_sessions[sid] = (name, if_match, behavior)
            return sid

    def finish_upload(self, sid: str, data: bytes) -> tuple[int, dict]:
        with self.lock:
            entry = self.od_sessions.pop(sid, None)
        if entry is None:
            return 404, {"error": {"code": "itemNotFound", "message": "Sitzung unbekannt"}}
        return self.put_content(entry[0], data, entry[1], entry[2])


class OneDriveHandler(Handler):
    fake: FakeOneDrive

    def _graph_error(self, status: int, code: str) -> None:
        self._json(status, {"error": {"code": code, "message": code}})

    def _route(self, method: str) -> None:
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query, keep_blank_values=True))
        path = url.path
        fake = self.fake
        with fake.lock:
            fake.requests.append(f"{method} {path}")
        try:
            body = self._body()
        except ValueError:
            self._json(413, {"error": "too_large"})
            return
        if self._oauth(method, path, body):
            return

        bearer = self.headers.get("Authorization")
        # Vorab signierte URLs (Download-Umleitung, Upload-Sitzung): ein mitgeschickter Bearer ist ein Fehler.
        if method == "GET" and path.startswith("/dl/"):
            data = fake.content(path[len("/dl/"):])
            if bearer:
                self._graph_error(400, "invalidRequest")
            elif data is None:
                self._graph_error(404, "itemNotFound")
            else:
                self._send(200, data, "application/zip")
            return
        if method == "PUT" and path.startswith("/up/"):
            want = f"bytes 0-{len(body) - 1}/{len(body)}"
            if bearer or not body or self.headers.get("Content-Range") != want:
                self._graph_error(400, "invalidRequest")
            else:
                self._json(*fake.finish_upload(path[len("/up/"):], body))
            return

        if not path.startswith("/v1.0/"):
            self._json(404, {"error": "not_found"})
            return
        if fake.offline_api:
            self.close_connection = True
            self.connection.close()
            return
        if not fake.authorized(bearer, path):
            self._graph_error(401, "InvalidAuthenticationToken")
            return

        if_match = self.headers.get("If-Match")
        if method == "GET" and path == OD_API:
            if fake.offline_after_link and fake.on_offline is not None:
                fake.offline_after_link = False
                fake.on_offline()
                self.close_connection = True
            self._json(200, {"id": OD_DRIVE_ID})
        elif path.startswith(OD_APPROOT):
            rest = path[len(OD_APPROOT):]
            if method == "PUT" and rest.endswith(":/content"):
                name = rest[:-len(":/content")]
                behavior = query.get("@microsoft.graph.conflictBehavior", "replace")
                self._json(*fake.put_content(name, body, if_match, behavior))
            elif method == "POST" and rest.endswith(":/createUploadSession"):
                try:
                    item = json.loads(body or b"{}").get("item", {})
                except (ValueError, AttributeError):
                    self._graph_error(400, "invalidRequest")
                    return
                res = fake.start_upload(rest[:-len(":/createUploadSession")], if_match,
                                        item.get("@microsoft.graph.conflictBehavior", "replace"))
                if isinstance(res, str):
                    self._json(200, {"uploadUrl": f"{self._base()}/up/{res}", "expirationDateTime": "2099-01-01T00:00:00Z"})
                else:
                    self._json(*res)
            elif method == "GET" and ":" not in rest:
                self._json(*fake.stat(rest))
            else:
                self._graph_error(400, "invalidRequest")
        elif path.startswith(OD_API + "/items/"):
            rest = path[len(OD_API + "/items/"):]
            if method == "GET" and rest.endswith("/content"):
                item_id = rest[:-len("/content")]
                if fake.content(item_id) is None:
                    self._graph_error(404, "itemNotFound")
                else:
                    self._send(302, b"", "text/plain", {"Location": f"{self._base()}/dl/{item_id}"})
            elif method == "PATCH" and "/" not in rest:
                try:
                    desc = json.loads(body)["description"]
                except (ValueError, KeyError, TypeError):
                    self._graph_error(400, "invalidRequest")
                    return
                self._json(*fake.set_description(rest, str(desc)))
            else:
                self._graph_error(400, "invalidRequest")
        else:
            self._json(404, {"error": "not_found"})


# --- Dropbox (M29) ----------------------------------------------------------------------------------------------------

DBX_ACCOUNT = "dbid:AAFakeAccount0001"
_DBX_CODE = re.compile(r'<code id="code">([A-Za-z0-9_-]+)</code>')


def dropbox_code_from_page(html: str) -> str:
    """Den Code aus der Freigabeseite des Fakes (wie ihn der Nutzer abliest)."""
    m = _DBX_CODE.search(html)
    if m is None:
        raise ValueError("kein Code auf der Seite")
    return m.group(1)


def _s256(verifier: str) -> str:
    import base64
    return base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()


def _dbx_error(summary: str) -> dict:
    return {"error_summary": summary + "/..", "error": {".tag": summary.split("/", 1)[0]}}


@dataclass
class DropboxFile:
    id: str
    name: str
    data: bytes
    rev: str

    def metadata(self, tag: bool) -> dict:
        out = {"name": self.name, "path_lower": "/" + self.name.lower(), "path_display": "/" + self.name,
               "id": self.id, "rev": self.rev, "size": len(self.data), "content_hash": sha256_hex(self.data),
               "is_downloadable": True}
        if tag:
            out = {".tag": "file", **out}
        return out


class FakeDropbox(FakeDrive):
    """Dropbox-Fake (M29): Freigabeseite /oauth2/authorize mit PKCE (S256), /oauth2/token (authorization_code,
    refresh_token), RPC unter /2/… mit JSON, Content-Endpunkte mit Dropbox-API-Arg, Fehler als 409 + error_summary,
    Widerruf per Bearer. Kein Metadaten-Feld: Hash, Gerät und Zeit stehen nur im ZIP."""

    TOKEN_PATH, REVOKE_PATH = "/oauth2/token", "/2/auth/token/revoke"
    ACCOUNT_PATH = "/2/users/get_current_account"
    SCOPE = "account_info.read files.content.read files.content.write"
    ACCESS_PREFIX, REFRESH_PREFIX, CODE_PREFIX = "sl.fake", "fake-rt-", "dbxcode_"

    def __init__(self, *args, token_delay_s: float = 0.0, **kwargs):
        super().__init__(*args, **kwargs)
        self.token_delay_s = token_delay_s  # pairtwice: der Code-Tausch dauert, die Kopplungsseite lebt noch
        self.codes: dict[str, str | None] = {}  # Code → Challenge (None = verbraucht)
        self.dbx: dict[str, DropboxFile] = {}  # Name → Datei

    def _rev(self) -> str:
        self._seq += 1
        return f"{self._seq:09x}{_secrets.token_hex(3)}"

    def seed(self, unit: str, data: bytes, device: str = "Dolphin", saved: int | None = None) -> DropboxFile:
        zip_meta(data)  # wie die anderen Fakes: nur gültige Pakete
        with self.lock:
            f = DropboxFile("id:" + _secrets.token_urlsafe(12), unit + ".zip", data, self._rev())
            self.dbx[f.name] = f
            self._dump()
            return f

    def file_bytes(self, unit: str) -> bytes | None:
        with self.lock:
            f = self.dbx.get(unit + ".zip")
            return f.data if f else None

    def state_dump(self) -> dict:
        files = {f.name: {"rev": f.rev, "size": len(f.data), "sha256": sha256_hex(f.data)} for f in self.dbx.values()}
        return {"account": DBX_ACCOUNT, "files": files, "uploads": self.uploads, "refreshes": self.refreshes,
                "linked": bool(self.refresh), "revokes": self.revokes}

    # --- OAuth ---
    def authorize(self, query: dict) -> tuple[int, str]:
        if not query.get("client_id") or query.get("response_type") != "code":
            return 400, "<p>invalid_request</p>"
        challenge = query.get("code_challenge", "")
        if query.get("code_challenge_method") != "S256" or not re.fullmatch(r"[A-Za-z0-9_-]{43}", challenge):
            return 400, "<p>invalid code_challenge</p>"
        with self.lock:
            code = self._issue(self.CODE_PREFIX)
            self.codes[code] = challenge
        return 200, (f'<!doctype html><title>Fake Dropbox</title><p>Gib diesen Code in die App ein:</p>'
                     f'<code id="code">{code}</code>')

    def token(self, form: dict) -> tuple[int, dict]:
        if form.get("grant_type") != "authorization_code":
            return super().token(form)
        if not form.get("client_id"):
            return 400, {"error": "invalid_client"}
        with self.lock:
            code = form.get("code", "")
            challenge = self.codes.get(code)
            if challenge is None or _s256(form.get("code_verifier", "")) != challenge:
                return 400, {"error": "invalid_grant", "error_description": "code doesn't exist or has expired"}
            self.codes[code] = None
            access, refresh = self._issue(self.ACCESS_PREFIX), self._issue(self.REFRESH_PREFIX)
            self.access.add(access)
            self.refresh.add(refresh)
            self._dump()
            return 200, {"access_token": access, "token_type": "bearer", "expires_in": 14400,
                         "refresh_token": refresh, "scope": self.SCOPE, "uid": "12345",
                         "account_id": DBX_ACCOUNT}

    def revoke_bearer(self) -> tuple[int, dict]:
        """/2/auth/token/revoke: der Bearer war gültig; mit ihm fällt die ganze Anmeldung (auch das Refresh-Token)."""
        with self.lock:
            self.access.clear()
            self.refresh.clear()
            self.revokes += 1
            self._dump()
            return 200, {}

    # --- Dateien ---
    def get_metadata(self, path: str) -> tuple[int, dict]:
        with self.lock:
            f = self.dbx.get(path.lstrip("/"))
            if f is None:
                return 409, _dbx_error("path/not_found")
            return 200, f.metadata(tag=True)

    def download(self, path: str) -> tuple[int, dict | bytes, dict]:
        with self.lock:
            if not path.startswith("rev:"):
                return 400, {"error": "nur rev:"}, {}
            f = next((x for x in self.dbx.values() if x.rev == path[4:]), None)
            if f is None:
                return 409, _dbx_error("path/not_found"), {}
            return 200, f.data, {"Dropbox-API-Result": json.dumps(f.metadata(tag=False))}

    def upload(self, arg: dict, data: bytes) -> tuple[int, dict]:
        name = str(arg.get("path", "")).lstrip("/")
        mode = arg.get("mode", "add")
        if not name or "/" in name or arg.get("autorename") is True:
            return 400, {"error": "path"}
        with self.lock:
            f = self.dbx.get(name)
            if mode == "add" or mode == {".tag": "add"}:
                if f is not None:
                    return 409, _dbx_error("path/conflict/file")
            elif isinstance(mode, dict) and mode.get(".tag") == "update":
                if f is None or f.rev != mode.get("update"):
                    # strict_conflict: auch eine gleich große, gleiche Datei zählt als Konflikt
                    return 409, _dbx_error("path/conflict/file")
            elif mode != "overwrite" and mode != {".tag": "overwrite"}:
                return 400, {"error": "mode"}
            if f is None:
                f = DropboxFile("id:" + _secrets.token_urlsafe(12), name, data, self._rev())
                self.dbx[name] = f
            else:
                f.data = data
                f.rev = self._rev()
            self.uploads += 1
            self._dump()
            return 200, f.metadata(tag=False)


class DropboxHandler(Handler):
    fake: FakeDropbox

    def _route(self, method: str) -> None:
        url = urllib.parse.urlsplit(self.path)
        query = dict(urllib.parse.parse_qsl(url.query, keep_blank_values=True))
        path = url.path
        fake = self.fake
        with fake.lock:
            fake.requests.append(f"{method} {path}")
        try:
            body = self._body()
        except ValueError:
            self._json(413, {"error": "too_large"})
            return

        if method == "GET" and path == "/oauth2/authorize":
            status, page = fake.authorize(query)
            self._send(status, page.encode(), "text/html; charset=utf-8")
            return
        if method == "POST" and path == fake.TOKEN_PATH:
            form = dict(urllib.parse.parse_qsl(body.decode("utf-8", "replace")))
            if fake.token_delay_s and form.get("grant_type") == "authorization_code":
                time.sleep(fake.token_delay_s)
            self._json(*fake.token(form))
            return
        if not path.startswith("/2/"):
            self._json(404, {"error": "not_found"})
            return
        if fake.offline_api:
            self.close_connection = True
            self.connection.close()
            return
        if method != "POST":
            self._json(405, {"error": "method"})
            return
        if not fake.authorized(self.headers.get("Authorization"), path):
            self._json(401, _dbx_error("invalid_access_token"))
            return

        if path == fake.REVOKE_PATH:
            self._json(*fake.revoke_bearer())
            return
        if path in ("/2/files/upload", "/2/files/download"):
            try:
                arg = json.loads(self.headers.get("Dropbox-API-Arg") or "")
            except ValueError:
                self._json(400, {"error": "Dropbox-API-Arg"})
                return
            if path == "/2/files/download":
                status, out, headers = fake.download(str(arg.get("path", "")))
                if isinstance(out, bytes):
                    self._send(status, out, "application/octet-stream", headers)
                else:
                    self._json(status, out)
            elif self.headers.get("Content-Type") != "application/octet-stream":
                self._json(400, {"error": "Content-Type"})
            else:
                self._json(*fake.upload(arg, body))
            return
        # RPC: JSON-Body ("null" für Aufrufe ohne Argument)
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            self._json(400, {"error": "Content-Type"})
            return
        try:
            arg = json.loads(body or b"null")
        except ValueError:
            self._json(400, {"error": "json"})
            return
        if path == fake.ACCOUNT_PATH:
            if fake.offline_after_link and fake.on_offline is not None:
                fake.offline_after_link = False
                fake.on_offline()
                self.close_connection = True
            self._json(200, {"account_id": DBX_ACCOUNT, "name": {"display_name": "Fake"}})
        elif path == "/2/files/get_metadata" and isinstance(arg, dict):
            self._json(*fake.get_metadata(str(arg.get("path", ""))))
        else:
            self._json(404, {"error": "not_found"})


def start(fake: FakeDrive, ssl_context: ssl.SSLContext | None = None, port: int = 0,
          host: str = "127.0.0.1") -> tuple[http.server.ThreadingHTTPServer, int]:
    """Startet den Fake in einem Thread; liefert Server und Port."""
    base = (OneDriveHandler if isinstance(fake, FakeOneDrive)
            else DropboxHandler if isinstance(fake, FakeDropbox) else Handler)
    handler = type("BoundHandler", (base,), {"fake": fake})
    server = http.server.ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    if ssl_context is not None:
        if ssl_context.minimum_version < ssl.TLSVersion.TLSv1_2:
            ssl_context.minimum_version = ssl.TLSVersion.TLSv1_2
        server.socket = ssl_context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def offline() -> None:  # läuft in einem Handler-Thread, nie in serve_forever selbst
        server.shutdown()
        server.socket.close()

    fake.on_offline = offline
    return server, server.server_address[1]


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--provider", choices=["drive", "onedrive", "dropbox"], default="drive")
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--cert", type=Path)
    p.add_argument("--key", type=Path)
    p.add_argument("--approve", choices=["auto", "manual", "deny"], default="auto")
    p.add_argument("--expire-access-once", action="store_true")
    p.add_argument("--offline-api", action="store_true")
    p.add_argument("--offline-after-link", action="store_true")
    p.add_argument("--seed", action="append", default=[], metavar="EINHEIT=ZIP")
    p.add_argument("--state-dump", type=Path)
    args = p.parse_args(argv)
    if (args.cert is None) != (args.key is None):
        p.error("--cert und --key nur zusammen")
    cls = {"onedrive": FakeOneDrive, "dropbox": FakeDropbox}.get(args.provider, FakeDrive)
    fake = cls(args.approve, args.expire_access_once, args.offline_api, args.offline_after_link)
    fake.dump_path = args.state_dump
    try:
        for spec in args.seed:
            unit, path = parse_seed(spec)
            fake.seed(unit, path.read_bytes())
    except (ValueError, OSError, KeyError, zipfile.BadZipFile) as e:
        print(f"cloud_fake: {e}", file=sys.stderr)
        return 2
    ctx = None
    if args.cert is not None:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(str(args.cert), str(args.key))
    server, port = start(fake, ctx, args.port)
    print(f"cloud_fake: {'https' if ctx else 'http'}://localhost:{port}", flush=True)
    fake._dump()
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
