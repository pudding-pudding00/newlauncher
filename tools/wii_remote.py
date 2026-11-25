"""Fernsteuerung des laufenden newlaunchers über TCP 4300 (M20): Deployment und Fehlersuche vom PC aus.

    python tools/wii_remote.py --host <IP> start [--dol PATH]       # per wiiload mit --remote starten (HBC muss laufen)
    python tools/wii_remote.py --host <IP> hello | state | games | log
    python tools/wii_remote.py --host <IP> shot bild.png            # Screenshot (XFB, YUYV → PNG)
    python tools/wii_remote.py --host <IP> key a [frames]           # Tasten: up,down,left,right,a,b,minus,plus,one,two,home,gc_*
    python tools/wii_remote.py --host <IP> launch <spielschlüssel>
    python tools/wii_remote.py --host <IP> put <lokal> <wii-pfad>   # nur <gerät>:/apps/newlauncher/… und den Datenordner
    python tools/wii_remote.py --host <IP> update [--dol PATH]      # boot.dol auf die SD schreiben, dann neu starten
    python tools/wii_remote.py --host <IP> restart | exit
    python tools/wii_remote.py --host <IP> hbc-install [--dol PATH] # App-ZIP per wiiload an den HBC (der entpackt nach sd:/apps/)

Statt --host geht die Umgebungsvariable NL_WII_HOST. update und hbc-install schreiben auf die SD-Karte der Wii.
Protokoll: siehe src/core/remote_proto.hpp.
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

from deploy_dolphin import DIST_DIR, REPO_ROOT
from notices import REPO_ROOT as NOTICES_ROOT, write_notices
from pngfile import write_png

REMOTE_PORT = 4300
RELEASE_DOL = REPO_ROOT / "build/wii/newlauncher.dol"
WIILOAD_EXE = Path(r"C:\devkitPro\tools\bin\wiiload.exe")
APP_BOOT = "sd:/apps/newlauncher/boot.dol"


class RemoteError(Exception):
    pass


def parse_header(line: bytes) -> tuple[bool, int, str]:
    parts = line.decode("utf-8", "replace").rstrip("\r\n").split(" ", 2)
    if len(parts) < 2 or parts[0] not in ("OK", "ERR") or not parts[1].isdigit():
        raise RemoteError(f"unerwartete Antwort: {line[:80]!r}")
    return parts[0] == "OK", int(parts[1]), parts[2] if len(parts) > 2 else ""


def _clamp(v: float) -> int:
    return 0 if v < 0 else 255 if v > 255 else int(v + 0.5)


def yuyv_to_rgba(data: bytes, w: int, h: int) -> bytes:
    """XFB (Y0 U Y1 V je Pixelpaar, BT.601 Studio-Bereich) → RGBA."""
    if len(data) != w * h * 2 or w % 2:
        raise RemoteError(f"Framebuffer {len(data)} Bytes passt nicht zu {w}x{h}")
    out = bytearray(w * h * 4)
    o = 0
    for i in range(0, len(data), 4):
        y0, u, y1, v = data[i], data[i + 1] - 128, data[i + 2], data[i + 3] - 128
        for y in (y0, y1):
            c = 1.164 * (y - 16)
            out[o:o + 4] = bytes((_clamp(c + 1.596 * v), _clamp(c - 0.392 * u - 0.813 * v), _clamp(c + 2.017 * u), 255))
            o += 4
    return bytes(out)


class RemoteClient:
    def __init__(self, host: str, port: int = REMOTE_PORT, timeout: float = 30.0):
        try:
            self.sock = socket.create_connection((host, port), timeout=timeout)
        except OSError as e:
            raise RemoteError(f"keine Verbindung zu {host}:{port} ({e}). Läuft newlauncher mit --remote?") from e
        self.file = self.sock.makefile("rb")

    def __enter__(self) -> RemoteClient:
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        self.file.close()
        self.sock.close()

    def request(self, line: str, payload: bytes = b"") -> tuple[bool, str, bytes]:
        self.sock.sendall(line.encode("utf-8") + b"\n" + payload)
        head = self.file.readline()
        if not head:
            raise RemoteError("Verbindung geschlossen")
        ok, length, info = parse_header(head)
        data = self.file.read(length)
        if len(data) != length:
            raise RemoteError("Antwort unvollständig")
        return ok, info, data

    def call(self, line: str, payload: bytes = b"") -> tuple[str, bytes]:
        ok, info, data = self.request(line, payload)
        if not ok:
            raise RemoteError(f"{line.split(' ', 1)[0]}: {info}")
        return info, data

    def put(self, data: bytes, path: str) -> str:
        return self.call(f"PUT {len(data)} {zlib.crc32(data):08x} {path}", data)[0]


def build_hbc_zip(dol: Path, out: Path) -> None:
    """ZIP für den HBC-Empfang: nur apps/newlauncher/ (Nutzerdaten unter newlauncher/ bleiben unberührt)."""
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dol, "apps/newlauncher/boot.dol")
        for name in ("meta.xml", "icon.png"):
            src = DIST_DIR / "apps/newlauncher" / name
            if src.is_file():
                z.write(src, f"apps/newlauncher/{name}")
        write_notices(NOTICES_ROOT, Path(tmp))
        for f in sorted(Path(tmp).iterdir()):
            z.write(f, f"apps/newlauncher/{f.name}")


def wiiload(path: Path, host: str, args: list[str]) -> int:
    env = dict(os.environ, WIILOAD=f"tcp:{host}")
    return subprocess.run([str(WIILOAD_EXE), str(path), *args], env=env).returncode


def _need_dol(dol: Path) -> Path:
    if not dol.is_file():
        raise RemoteError(f"DOL nicht gefunden: {dol} (python tools/build_wii.py)")
    return dol


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Fernsteuerung für newlauncher (M20)")
    parser.add_argument("--host", default=os.environ.get("NL_WII_HOST"), help="IP der Wii (sonst NL_WII_HOST)")
    parser.add_argument("--port", type=int, default=REMOTE_PORT)
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in ("hello", "state", "games", "log", "restart", "exit"):
        sub.add_parser(name)
    p = sub.add_parser("shot")
    p.add_argument("out", type=Path)
    p = sub.add_parser("key")
    p.add_argument("buttons")
    p.add_argument("frames", nargs="?", type=int)
    p = sub.add_parser("launch")
    p.add_argument("key")
    p = sub.add_parser("put")
    p.add_argument("local", type=Path)
    p.add_argument("remote")
    for name in ("update", "start", "hbc-install"):
        p = sub.add_parser(name)
        p.add_argument("--dol", type=Path, default=RELEASE_DOL)
    args = parser.parse_args(argv)
    if not args.host:
        parser.error("--host oder NL_WII_HOST fehlt")

    try:
        if args.cmd == "start":
            return wiiload(_need_dol(args.dol), args.host, ["--remote"])
        if args.cmd == "hbc-install":
            with tempfile.TemporaryDirectory() as tmp:
                z = Path(tmp) / "newlauncher.zip"
                build_hbc_zip(_need_dol(args.dol), z)
                return wiiload(z, args.host, [])
        with RemoteClient(args.host, args.port) as c:
            if args.cmd in ("hello", "state", "games", "log"):
                info, data = c.call(args.cmd.upper())
                if info:
                    print(info)
                sys.stdout.write(data.decode("utf-8", "replace"))
            elif args.cmd == "shot":
                info, data = c.call("SHOT")
                w, h, fmt = info.split(" ")
                if fmt != "yuyv":
                    raise RemoteError(f"Format {fmt} unbekannt")
                write_png(args.out, int(w), int(h), yuyv_to_rgba(data, int(w), int(h)))
                print(f"{args.out} ({w}x{h})")
            elif args.cmd == "key":
                c.call(f"KEY {args.buttons}" + (f" {args.frames}" if args.frames else ""))
            elif args.cmd == "launch":
                print(c.call(f"LAUNCH {args.key}")[0])
            elif args.cmd == "put":
                print(c.put(args.local.read_bytes(), args.remote))
            elif args.cmd == "update":
                c.put(_need_dol(args.dol).read_bytes(), APP_BOOT)
                print(f"{APP_BOOT} geschrieben, Neustart: {c.call('RESTART')[0]}")
            elif args.cmd in ("restart", "exit"):
                print(c.call(args.cmd.upper())[0])
    except RemoteError as e:
        print(f"Fehler: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
