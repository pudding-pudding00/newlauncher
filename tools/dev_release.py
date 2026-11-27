"""Version numbers of the development releases that are published on every merge into main.

    python tools/dev_release.py next-version [--root REPO] [--rev REV]
    python tools/dev_release.py check-package ZIP VERSION

Development releases are numbered 0.0.1, 0.0.2, ... Each one is tagged dev-<version> on its merge commit.
A commit that already carries a dev tag keeps that number, so re-running the publish job does not number it again.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import zipfile
from collections.abc import Iterable
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
TAG_RE = re.compile(r"^dev-0\.0\.(\d+)$")
META_VERSION_RE = re.compile(r"<version>\s*([^<]*?)\s*</version>")
META_PATH = "apps/newlauncher/meta.xml"
# The largest ZIP the launcher accepts in update.ini: kMaxUpdateZipBytes in src/core/self_update.hpp (a test checks it).
MAX_ZIP_BYTES = 32 * 1024 * 1024


def _number(tag: str) -> int | None:
    match = TAG_RE.match(tag)
    return int(match.group(1)) if match else None


def next_dev_version(tags: Iterable[str]) -> str:
    """The version one above the newest dev tag; 0.0.1 when there is none."""
    numbers = [n for n in map(_number, tags) if n is not None]
    return f"0.0.{max(numbers, default=0) + 1}"


def resolve_version(all_tags: Iterable[str], tags_at_rev: Iterable[str]) -> str:
    """The version a commit already has (its dev tag), otherwise the next one."""
    for tag in tags_at_rev:
        number = _number(tag)
        if number is not None:
            return f"0.0.{number}"
    return next_dev_version(all_tags)


def _git(root: Path, *args: str) -> list[str]:
    # The CI container runs as root in a workspace owned by the runner user, and git refuses such a repository
    # ("dubious ownership"). safe.directory is honoured on the command line, so whitelist this repository there.
    cmd = ["git", "-c", f"safe.directory={root}", "-C", str(root), *args]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line]


def repo_dev_tags(root: Path) -> list[str]:
    """All dev tags of the repository, oldest number first."""
    tags = [t for t in _git(root, "tag", "--list", "dev-0.0.*") if _number(t) is not None]
    return sorted(tags, key=_number)


def version_for_rev(root: Path, rev: str) -> str:
    """The development version for the commit `rev`."""
    tags_at_rev = [t for t in _git(root, "tag", "--points-at", rev) if _number(t) is not None]
    return resolve_version(repo_dev_tags(root), tags_at_rev)


def package_version(zip_path: Path) -> str | None:
    """The version written to apps/newlauncher/meta.xml inside a release ZIP, or None."""
    with zipfile.ZipFile(zip_path) as z:
        try:
            meta = z.read(META_PATH).decode("utf-8")
        except KeyError:
            return None
    match = META_VERSION_RE.search(meta)
    return match.group(1) if match else None


def check_package(zip_path: Path, version: str) -> list[str]:
    """What is wrong with a release ZIP; an empty list means it can be published."""
    problems = []
    found = package_version(zip_path)
    if found != version:
        problems.append(f"{META_PATH} has version {found!r}, expected {version!r}")
    size = zip_path.stat().st_size
    if size > MAX_ZIP_BYTES:
        problems.append(f"the ZIP is {size} bytes, the launcher accepts at most {MAX_ZIP_BYTES}")
    return problems


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Version numbers of the development releases")
    sub = ap.add_subparsers(dest="command", required=True)
    nv = sub.add_parser("next-version", help="print the development version for a commit")
    nv.add_argument("--root", type=Path, default=REPO_ROOT)
    nv.add_argument("--rev", default="HEAD")
    cp = sub.add_parser("check-package", help="check a release ZIP: version in meta.xml and size the launcher accepts")
    cp.add_argument("zip", type=Path)
    cp.add_argument("version")
    args = ap.parse_args(argv)

    if args.command == "next-version":
        print(version_for_rev(args.root, args.rev))
        return 0
    problems = check_package(args.zip, args.version)
    for problem in problems:
        print(f"{args.zip.name}: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"{args.zip.name}: version {args.version} and size ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
