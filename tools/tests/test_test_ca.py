"""Tests für tools/test_ca.py (M11 Task 7): Test-CA zur Laufzeit, keine Schlüssel im Repo."""

import shutil
import socket
import ssl
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import test_ca  # noqa: E402

HAVE_OPENSSL = shutil.which("openssl") is not None


@unittest.skipUnless(HAVE_OPENSSL, "openssl nicht im PATH")
class TestCaTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_make_writes_ca_and_server_files(self):
        paths = test_ca.make(self.dir)
        for key in ("ca", "cert", "key"):
            self.assertTrue(paths[key].is_file(), key)
            self.assertEqual(paths[key].parent, self.dir)
        self.assertIn(b"BEGIN CERTIFICATE", paths["ca"].read_bytes())
        self.assertIn(b"PRIVATE KEY", paths["key"].read_bytes())

    def test_client_trusting_ca_connects_to_localhost(self):
        paths = test_ca.make(self.dir)
        server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server_ctx.load_cert_chain(paths["cert"], paths["key"])
        listener = socket.create_server(("127.0.0.1", 0))
        port = listener.getsockname()[1]
        errors = []

        def serve():
            conn, _ = listener.accept()
            try:
                with server_ctx.wrap_socket(conn, server_side=True) as tls:
                    tls.sendall(b"ok")
            except (OSError, ssl.SSLError) as e:  # pragma: no cover - nur bei Fehlschlag
                errors.append(e)

        t = threading.Thread(target=serve, daemon=True)
        t.start()
        client_ctx = ssl.create_default_context(cafile=str(paths["ca"]))
        with socket.create_connection(("127.0.0.1", port), timeout=10) as raw:
            with client_ctx.wrap_socket(raw, server_hostname="localhost") as tls:
                self.assertEqual(tls.recv(2), b"ok")
        t.join(10)
        listener.close()
        self.assertEqual(errors, [])

    def test_other_ca_does_not_verify_server(self):
        paths = test_ca.make(self.dir / "a")
        other = test_ca.make(self.dir / "b")
        server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server_ctx.load_cert_chain(paths["cert"], paths["key"])
        listener = socket.create_server(("127.0.0.1", 0))
        port = listener.getsockname()[1]

        def serve():
            conn, _ = listener.accept()
            try:
                server_ctx.wrap_socket(conn, server_side=True).close()
            except (OSError, ssl.SSLError):
                pass

        t = threading.Thread(target=serve, daemon=True)
        t.start()
        client_ctx = ssl.create_default_context(cafile=str(other["ca"]))
        with socket.create_connection(("127.0.0.1", port), timeout=10) as raw:
            with self.assertRaises(ssl.SSLCertVerificationError):
                client_ctx.wrap_socket(raw, server_hostname="localhost")
        t.join(10)
        listener.close()


class TestCaCliTest(unittest.TestCase):
    def test_without_openssl_exits_2_with_message(self):
        with tempfile.TemporaryDirectory() as d, mock.patch("test_ca.shutil.which", return_value=None):
            with mock.patch("sys.stdout") as out:
                rc = test_ca.main(["make", d])
        self.assertEqual(rc, 2)
        printed = "".join(str(c.args[0]) for c in out.write.call_args_list)
        self.assertIn("openssl", printed)


if __name__ == "__main__":
    unittest.main()
