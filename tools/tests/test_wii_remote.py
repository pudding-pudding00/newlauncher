import socket
import sys
import tempfile
import threading
import unittest
import zipfile
import zlib
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from wii_remote import (RemoteClient, RemoteError, build_hbc_zip, main, parse_header,  # noqa: E402
                        yuyv_to_rgba)


class FakeWii:
    """Ein-Verbindungs-Server mit dem Protokoll aus core/remote_proto: antwortet aus einer Tabelle."""

    def __init__(self, replies: dict[str, tuple[bool, str, bytes]]):
        self.replies = replies
        self.received: list[tuple[str, bytes]] = []
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        conn, _ = self.sock.accept()
        f = conn.makefile("rb")
        with conn:
            for raw in f:
                line = raw.decode().rstrip("\n")
                verb = line.split(" ", 1)[0]
                payload = b""
                if verb == "PUT":
                    payload = f.read(int(line.split(" ")[1]))
                self.received.append((line, payload))
                ok, info, data = self.replies.get(verb, (False, "unbekannt", b""))
                head = f"{'OK' if ok else 'ERR'} {len(data)}" + (f" {info}" if info else "") + "\n"
                conn.sendall(head.encode() + data)

    def close(self):
        self.sock.close()


class ParseHeaderTest(unittest.TestCase):
    def test_ok_with_info(self):
        self.assertEqual(parse_header(b"OK 12 640 480 yuyv\n"), (True, 12, "640 480 yuyv"))

    def test_err_without_info(self):
        self.assertEqual(parse_header(b"ERR 0\n"), (False, 0, ""))

    def test_garbage_raises(self):
        with self.assertRaises(RemoteError):
            parse_header(b"HALLO\n")


class YuyvTest(unittest.TestCase):
    def test_white_and_black(self):
        # Ein YUYV-Paar: zwei Pixel, Y0=235 (weiß), Y1=16 (schwarz), U=V=128 (grau-neutral).
        rgba = yuyv_to_rgba(bytes([235, 128, 16, 128]), 2, 1)
        self.assertEqual(rgba, bytes([255, 255, 255, 255, 0, 0, 0, 255]))

    def test_size_mismatch_raises(self):
        with self.assertRaises(RemoteError):
            yuyv_to_rgba(b"\0" * 6, 2, 1)


class ClientTest(unittest.TestCase):
    def test_request_reads_payload(self):
        wii = FakeWii({"LOG": (True, "", b"NL_READY\n")})
        try:
            with RemoteClient("127.0.0.1", wii.port) as c:
                self.assertEqual(c.request("LOG"), (True, "", b"NL_READY\n"))
        finally:
            wii.close()

    def test_put_sends_size_crc_and_bytes(self):
        wii = FakeWii({"PUT": (True, "5", b"")})
        try:
            with RemoteClient("127.0.0.1", wii.port) as c:
                c.put(b"hallo", "sd:/apps/newlauncher/boot.dol")
            wii.thread.join(2)
            line, payload = wii.received[0]
            self.assertEqual(line, f"PUT 5 {zlib.crc32(b'hallo'):08x} sd:/apps/newlauncher/boot.dol")
            self.assertEqual(payload, b"hallo")
        finally:
            wii.close()

    def test_err_raises_with_reason(self):
        wii = FakeWii({"LAUNCH": (False, "Spiel unbekannt: x", b"")})
        try:
            with RemoteClient("127.0.0.1", wii.port) as c:
                with self.assertRaisesRegex(RemoteError, "Spiel unbekannt"):
                    c.call("LAUNCH x")
        finally:
            wii.close()


class HbcZipTest(unittest.TestCase):
    def test_zip_has_app_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            dol = Path(tmp) / "nl.dol"
            dol.write_bytes(b"DOL")
            out = Path(tmp) / "nl.zip"
            build_hbc_zip(dol, out)
            with zipfile.ZipFile(out) as z:
                names = set(z.namelist())
                self.assertIn("apps/newlauncher/boot.dol", names)
                self.assertIn("apps/newlauncher/meta.xml", names)
                self.assertEqual(z.read("apps/newlauncher/boot.dol"), b"DOL")
                self.assertFalse(any(n.startswith("newlauncher/") for n in names))  # Nutzerdaten nie


class MainTest(unittest.TestCase):
    def test_update_puts_dol_then_restarts(self):
        wii = FakeWii({"PUT": (True, "3", b""), "RESTART": (True, "sd:/apps/newlauncher/boot.dol", b"")})
        try:
            with tempfile.TemporaryDirectory() as tmp:
                dol = Path(tmp) / "nl.dol"
                dol.write_bytes(b"DOL")
                self.assertEqual(main(["--host", "127.0.0.1", "--port", str(wii.port), "update", "--dol", str(dol)]), 0)
            wii.thread.join(2)
            self.assertEqual([r[0].split(" ")[0] for r in wii.received], ["PUT", "RESTART"])
            self.assertTrue(wii.received[0][0].endswith(" sd:/apps/newlauncher/boot.dol"))
        finally:
            wii.close()

    def test_start_runs_wiiload_with_remote_flag(self):
        with mock.patch("wii_remote.subprocess.run") as run:
            run.return_value.returncode = 0
            with tempfile.TemporaryDirectory() as tmp:
                dol = Path(tmp) / "nl.dol"
                dol.write_bytes(b"DOL")
                self.assertEqual(main(["--host", "10.0.0.2", "start", "--dol", str(dol)]), 0)
            cmd = run.call_args.args[0]
            self.assertEqual(cmd[1:], [str(dol), "--remote"])
            self.assertEqual(run.call_args.kwargs["env"]["WIILOAD"], "tcp:10.0.0.2")

    def test_host_required(self):
        with mock.patch.dict("os.environ", {}, clear=True):
            with self.assertRaises(SystemExit):
                main(["hello"])


if __name__ == "__main__":
    unittest.main()
