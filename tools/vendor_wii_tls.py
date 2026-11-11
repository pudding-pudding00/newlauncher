"""Vendort wii_tls nach third_party/wii_tls (M11, Spec v2 4.1).

Aufruf: python tools/vendor_wii_tls.py <wii_tls-repo> [--rev HEAD] [--dest third_party/wii_tls]

Nimmt den Stand per `git archive` (nur committete Dateien, keine Build-Ordner), lässt Tests,
Benchmarks, Beispiele und Doku weg und schreibt VENDORED.txt mit Commit, Datum und Quelle.
Ein Repo mit uncommitteten Änderungen wird abgelehnt, damit der vendorte Stand reproduzierbar bleibt.
"""

import argparse
import io
import shutil
import subprocess
import sys
import tarfile
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = REPO_ROOT / "third_party" / "wii_tls"
DROP_DIRS = ("tests", "bench", "examples", "docs")


def _git(repo: Path, *args: str, binary: bool = False):
    out = subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)
    return out.stdout if binary else out.stdout.decode("utf-8", "replace").strip()


def strip_credentials(url: str) -> str:
    """Entfernt Benutzer und Passwort aus einer Remote-URL; Nicht-URLs bleiben unverändert."""
    parts = urlsplit(url)
    if not parts.scheme or "@" not in parts.netloc:
        return url
    host = parts.netloc.rsplit("@", 1)[1]
    return urlunsplit((parts.scheme, host, parts.path, parts.query, parts.fragment))


def _drop(dest: Path) -> None:
    for child in list(dest.iterdir()):
        if child.is_dir() and (child.name in DROP_DIRS or child.name.startswith("build-")):
            shutil.rmtree(child)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("repo", type=Path)
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    args = ap.parse_args(argv)

    repo = args.repo.resolve()
    try:
        dirty = _git(repo, "status", "--porcelain")
        sha = _git(repo, "rev-parse", f"{args.rev}^{{commit}}")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"VENDOR FEHLER: {repo} ist kein lesbares Git-Repo ({e})")
        return 1
    if dirty:
        print(f"VENDOR FEHLER: {repo} hat uncommittete Änderungen:\n{dirty}")
        return 1
    try:
        remote = strip_credentials(_git(repo, "remote", "get-url", "origin"))
    except subprocess.CalledProcessError:
        remote = "unbekannt"

    tar_bytes = _git(repo, "archive", "--format=tar", sha, binary=True)
    dest = args.dest
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as tf:
        tf.extractall(dest, filter="data")
    _drop(dest)

    date = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    (dest / "VENDORED.txt").write_text(
        f"commit={sha}\ndate={date}\nsource={remote}\n"
        f"removed={','.join(DROP_DIRS)},build-*\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"wii_tls {sha[:7]} nach {dest} vendort")
    return 0


if __name__ == "__main__":
    sys.exit(main())
