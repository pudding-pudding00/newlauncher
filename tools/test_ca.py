"""Test-CA für HTTPS-Smokes (M11, Spec v2 4.2): CA und Serverzertifikat für localhost, zur Laufzeit erzeugt.

Aufruf: python tools/test_ca.py make <ordner>

Schreibt ca.pem, server.pem und server.key (EC P-256, 2 Tage gültig) nach <ordner>. Die Schlüssel entstehen nur
dort – das Repo enthält keine privaten Schlüssel. Braucht openssl im PATH (Git for Windows bringt es mit).
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

DAYS = "2"

# Konfiguration statt -subj: MSYS-Werkzeuge würden "/CN=…" sonst als Pfad umschreiben.
CA_CONFIG = """[req]
distinguished_name = dn
prompt = no
x509_extensions = v3_ca
[dn]
CN = newlauncher Test-CA
[v3_ca]
basicConstraints = critical, CA:TRUE
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash
"""

SERVER_CONFIG = """[req]
distinguished_name = dn
prompt = no
[dn]
CN = localhost
"""

SERVER_EXT = """basicConstraints = CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = serverAuth
subjectAltName = DNS:localhost
authorityKeyIdentifier = keyid
"""


class OpensslMissing(RuntimeError):
    pass


def _openssl() -> str:
    exe = shutil.which("openssl")
    if exe is None:
        raise OpensslMissing("openssl nicht im PATH (Git for Windows: …/Git/mingw64/bin)")
    return exe


def _run(exe: str, *args: str, cwd: Path) -> None:
    subprocess.run([exe, *args], cwd=cwd, check=True, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def make(root: Path) -> dict[str, Path]:
    """Erzeugt CA und Serverzertifikat in root; gibt die Pfade ca, cert und key zurück."""
    exe = _openssl()
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    (root / "ca.cnf").write_text(CA_CONFIG, encoding="ascii")
    (root / "server.cnf").write_text(SERVER_CONFIG, encoding="ascii")
    (root / "server.ext").write_text(SERVER_EXT, encoding="ascii")
    ec = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1", "-nodes"]
    _run(exe, "req", "-x509", *ec, "-keyout", "ca.key", "-out", "ca.pem", "-days", DAYS,
         "-config", "ca.cnf", cwd=root)
    _run(exe, "req", "-new", *ec, "-keyout", "server.key", "-out", "server.csr", "-config", "server.cnf", cwd=root)
    _run(exe, "x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
         "-out", "server.pem", "-days", DAYS, "-sha256", "-extfile", "server.ext", cwd=root)
    return {"ca": root / "ca.pem", "cert": root / "server.pem", "key": root / "server.key"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    mk = sub.add_parser("make", help="CA und Serverzertifikat erzeugen")
    mk.add_argument("dir", type=Path)
    args = parser.parse_args(argv)
    try:
        paths = make(args.dir)
    except OpensslMissing as e:
        print(f"TEST-CA FEHLER: {e}")
        return 2
    except subprocess.CalledProcessError as e:
        print(f"TEST-CA FEHLER: openssl: {e.stderr.decode(errors='replace').strip()}")
        return 1
    for name, path in paths.items():
        print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
