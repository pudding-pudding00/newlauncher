"""Mbed-TLS-Quelle für den Wii-Build unter Windows (M11).

wii_tls holt Mbed TLS 4.1.2 per FetchContent. Das Archiv enthält 147 Symlinks (alle unter
tf-psa-crypto/drivers/pqcp/mldsa-native/examples), die das tar der devkitPro-MSYS2-Shell nicht anlegen
kann, deshalb bricht das Entpacken dort ab. Unter Windows entpackt dieses Modul das Archiv selbst, prüft
denselben SHA256 wie wii_tls, lässt die Links weg und reicht den Ordner per
FETCHCONTENT_SOURCE_DIR_MBEDTLS weiter. Unter Linux (CI) bleibt es beim normalen FetchContent.
"""

import hashlib
import shutil
import tarfile
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION = "4.1.2"
URL = f"https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-{VERSION}/mbedtls-{VERSION}.tar.bz2"
SHA256 = "392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126"
CACHE_DIR = REPO_ROOT / "build" / "mbedtls"


def extract_without_symlinks(archive: Path, sha256: str, dest: Path) -> bool:
    """Entpackt `archive` ohne die oberste Ordnerebene nach `dest`. Falscher Hash: nichts entpacken."""
    if hashlib.sha256(archive.read_bytes()).hexdigest() != sha256:
        return False
    tmp = dest.with_name(dest.name + ".part")
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    with tarfile.open(archive) as tf:
        members = []
        for m in tf.getmembers():
            parts = m.name.split("/", 1)
            if m.issym() or m.islnk() or len(parts) < 2 or not parts[1]:
                continue
            m.name = parts[1]
            members.append(m)
        tf.extractall(tmp, members=members, filter="data")
    if dest.exists():
        shutil.rmtree(dest)
    tmp.rename(dest)
    return True


def ensure_source(cache_dir: Path = CACHE_DIR) -> Path:
    """Liefert den entpackten Quellordner; lädt und entpackt beim ersten Aufruf."""
    src = cache_dir / f"mbedtls-{VERSION}"
    if (src / "CMakeLists.txt").is_file():
        return src
    cache_dir.mkdir(parents=True, exist_ok=True)
    archive = cache_dir / f"mbedtls-{VERSION}.tar.bz2"
    if not archive.is_file():
        with urllib.request.urlopen(URL, timeout=60) as r:
            archive.write_bytes(r.read())
    if not extract_without_symlinks(archive, SHA256, src):
        archive.unlink()
        raise RuntimeError(f"SHA256 von {URL} stimmt nicht")
    return src
