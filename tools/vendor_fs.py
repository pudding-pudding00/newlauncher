"""Vendort Dateisystem-Bibliotheken nach third_party/<name>/upstream (M16, Spec v2 9.1 und 13).

Aufruf:
    python tools/vendor_fs.py libntfs <repo> [--dest third_party/libntfs] [--record]

Ablauf: `git archive` des festen Commits aus COMPONENTS, nur die Pfade aus `keep` ohne `exclude`.
Jede Datei wird gegen `<dest>/UPSTREAM.sha256` geprüft (falscher oder fehlender Hash → Abbruch mit
Dateiname, nichts wird geändert). Danach werden `<dest>/patches/*.patch` in Namensreihenfolge mit
`git apply` angewendet, das Ergebnis ersetzt `<dest>/upstream/`, und `VENDORED.txt` hält Commit,
Quelle, Lizenz und Patches fest. `--record` schreibt das Manifest neu (nur beim bewussten Update).
Eigene Dateien in `<dest>` (CMakeLists.txt, nl_compat.h, patches/) fasst das Skript nicht an.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = "UPSTREAM.sha256"


@dataclass
class Component:
    rev: str
    source: str
    license: str
    keep: tuple[str, ...]
    exclude: tuple[str, ...] = ()


COMPONENTS: dict[str, Component] = {
    # Spike docs/superpowers/spikes/m16-dateisysteme.md: ae00fc8 statt HEAD, realpath.* nicht vendort.
    "libntfs": Component(
        rev="ae00fc8f22a5ba7d9db73755570687299a7dd64b",
        source="https://github.com/extremscorner/libntfs.git",
        license="GPL-2.0-or-later (cache2.c, bit_ops.h: BSD-3-Clause)",
        keep=("source", "include", "LICENSE", "AUTHORS", "CREDITS", "README.md"),
        exclude=("source/realpath.c", "source/realpath.h"),
    ),
}


def _git(repo: Path, *args: str, binary: bool = False):
    out = subprocess.run(["git", "-c", "core.autocrlf=false", "-C", str(repo), *args], check=True, capture_output=True)
    return out.stdout if binary else out.stdout.decode("utf-8", "replace").strip()


def _wanted(rel: str, comp: Component) -> bool:
    top = PurePosixPath(rel).parts[0]
    return top in comp.keep and rel not in comp.exclude


def _extract(repo: Path, comp: Component, stage: Path) -> list[str]:
    tar_bytes = _git(repo, "archive", "--format=tar", comp.rev, binary=True)
    files = []
    with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as tf:
        members = [m for m in tf.getmembers() if m.isfile() and _wanted(m.name, comp)]
        tf.extractall(stage, members=members, filter="data")
        files = sorted(m.name for m in members)
    return files


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _read_manifest(path: Path) -> dict[str, str]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            digest, rel = line.split(None, 1)
            out[rel.strip()] = digest
    return out


def _verify(stage: Path, files: list[str], manifest: dict[str, str]) -> list[str]:
    bad = [f"{rel}: Hash falsch" for rel in files if manifest.get(rel) != _sha(stage / rel) and rel in manifest]
    bad += [f"{rel}: nicht im Manifest" for rel in files if rel not in manifest]
    bad += [f"{rel}: fehlt im Commit" for rel in manifest if rel not in files]
    return bad


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("name", choices=sorted(COMPONENTS))
    ap.add_argument("repo", type=Path)
    ap.add_argument("--dest", type=Path)
    ap.add_argument("--record", action="store_true", help="UPSTREAM.sha256 neu schreiben statt prüfen")
    args = ap.parse_args(argv)

    comp = COMPONENTS[args.name]
    dest = args.dest or REPO_ROOT / "third_party" / args.name
    repo = args.repo.resolve()
    try:
        sha = _git(repo, "rev-parse", "--verify", f"{comp.rev}^0")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"VENDOR FEHLER: Commit {comp.rev} nicht in {repo} ({e})")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "upstream"
        stage.mkdir()
        files = _extract(repo, comp, stage)
        manifest_path = dest / MANIFEST
        if args.record:
            dest.mkdir(parents=True, exist_ok=True)
            manifest_path.write_text("".join(f"{_sha(stage / r)}  {r}\n" for r in files),
                                     encoding="utf-8", newline="\n")
            print(f"{MANIFEST} mit {len(files)} Dateien geschrieben")
            return 0
        if not manifest_path.is_file():
            print(f"VENDOR FEHLER: {manifest_path} fehlt (erst mit --record anlegen)")
            return 1
        bad = _verify(stage, files, _read_manifest(manifest_path))
        if bad:
            print("VENDOR FEHLER: Upstream weicht vom Manifest ab:\n  " + "\n  ".join(bad))
            return 1

        patches = sorted((dest / "patches").glob("*.patch")) if (dest / "patches").is_dir() else []
        for p in patches:
            r = subprocess.run(["git", "-c", "core.autocrlf=false", "apply", "--whitespace=nowarn", str(p.resolve())], cwd=stage,
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(f"VENDOR FEHLER: Patch {p.name} passt nicht:\n{r.stderr}")
                return 1

        target = dest / "upstream"
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(stage, target)

    date = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    (dest / "VENDORED.txt").write_text(
        f"name={args.name}\ncommit={sha}\ndate={date}\nsource={comp.source}\nlicense={comp.license}\n"
        f"removed={','.join(comp.exclude) or '-'}\npatches={','.join(p.name for p in patches) or '-'}\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"{args.name} {sha[:7]} nach {dest / 'upstream'} vendort ({len(patches)} Patches)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
