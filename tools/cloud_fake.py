"""Google-Drive-Fake für die Cloud-Smokes (M27): Gerätecode, Tokens und Dateien im Speicher, optional über TLS.

Bildet genau die Anfragen nach, die src/core/oauth_device.cpp und src/core/drive_provider.cpp senden:
  POST /device/code, POST /token (device_code, refresh_token)
  GET  /drive/v3/about, GET/POST /drive/v3/files, GET /drive/v3/files/<id>?alt=media
  POST /upload/drive/v3/files, PATCH /upload/drive/v3/files/<id> (multipart oder resumable + PUT)

Aufruf: python tools/cloud_fake.py [--port N] [--cert server.pem --key server.key] [--approve auto|manual|deny]
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
                "linked": bool(self.refresh)}

    def secrets(self) -> list[str]:
        with self.lock:
            return list(self.issued)

    # --- OAuth ---
    def device_code(self, form: dict) -> tuple[int, dict]:
        if not form.get("client_id"):
            return 401, {"error": "invalid_client"}
        with self.lock:
            code = self._issue("AH-fakedev")
            self.devices[code] = {"auto": "approved", "deny": "denied"}.get(self.approve, "pending")
        return 200, {"device_code": code, "user_code": "FAKE-CODE", "verification_url": "https://www.google.com/device",
                     "expires_in": 1800, "interval": 1}

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
                    return 428, {"error": "authorization_pending"}
                if state == "denied":
                    return 403, {"error": "access_denied"}
                self.devices[form["device_code"]] = "used"
                access, refresh = self._issue("ya29.fake"), self._issue("1//fake")
                self.access.add(access)
                self.refresh.add(refresh)
                return 200, {"access_token": access, "refresh_token": refresh, "expires_in": 3599,
                             "scope": SCOPE, "token_type": "Bearer"}
            if grant == "refresh_token":
                if form.get("refresh_token") not in self.refresh:
                    return 400, {"error": "invalid_grant"}
                access = self._issue("ya29.fake")
                self.access.add(access)
                self.refreshes += 1
                self._dump()
                return 200, {"access_token": access, "expires_in": 3599, "scope": SCOPE, "token_type": "Bearer"}
        return 400, {"error": "unsupported_grant_type"}

    def authorized(self, header: str | None, path: str = "") -> bool:
        if not header or not header.startswith("Bearer "):
            return False
        tok = header[len("Bearer "):]
        with self.lock:
            if tok not in self.access:
                return False
            # about gehört zur Anmeldung; abgelaufen wird erst der Token für Datei-Anfragen
            if self.expire_access_once and not self._expired_once and path != "/drive/v3/about":
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


class Handler(http.server.BaseHTTPRequestHandler):
    fake: FakeDrive  # wird von start() gesetzt
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):  # leise
        pass

    def _send(self, status: int, body: bytes, ctype: str = "application/json", headers: dict | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
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

        if method == "POST" and path in ("/device/code", "/token"):
            form = dict(urllib.parse.parse_qsl(body.decode("utf-8", "replace")))
            status, obj = (fake.device_code if path == "/device/code" else fake.token)(form)
            self._json(status, obj)
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


def start(fake: FakeDrive, ssl_context: ssl.SSLContext | None = None, port: int = 0,
          host: str = "127.0.0.1") -> tuple[http.server.ThreadingHTTPServer, int]:
    """Startet den Fake in einem Thread; liefert Server und Port."""
    handler = type("BoundHandler", (Handler,), {"fake": fake})
    server = http.server.ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    if ssl_context is not None:
        server.socket = ssl_context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def offline() -> None:  # läuft in einem Handler-Thread, nie in serve_forever selbst
        server.shutdown()
        server.socket.close()

    fake.on_offline = offline
    return server, server.server_address[1]


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
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
    fake = FakeDrive(args.approve, args.expire_access_once, args.offline_api, args.offline_after_link)
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
