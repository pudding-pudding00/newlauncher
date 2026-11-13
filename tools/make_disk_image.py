"""Baut ein Platten-Image mit MBR und Partitionen für Smokes mit der SD als Rohgerät (M16, Spec v2 15).

    python tools/make_disk_image.py OUT --part fat32:<ordner> --part ntfs[:<ordner>] [--native]

Partitionen in der angegebenen Reihenfolge, je auf 1 MiB ausgerichtet, höchstens vier (primär).
FAT32 (mkfs.fat + mcopy), NTFS (mkntfs, gefüllt über ntfs-3g) und ext4 (mkfs.ext4 -d) entstehen lokal
in einem Docker-Container (tools/docker/fsimg.Dockerfile), mit --native direkt (Linux, root für FUSE).
Das Ergebnis liegt nur außerhalb des Repos; Images werden nie eingecheckt.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DOCKERFILE = REPO_ROOT / "tools" / "docker" / "fsimg.Dockerfile"
DOCKER_IMAGE = "nl-fsimg"
ALIGN = 2048  # Sektoren (1 MiB)
TYPES = {"fat32": 0x0C, "ntfs": 0x07, "ext4": 0x83}
MIN_MIB = {"fat32": 64, "ntfs": 16, "ext4": 16}


@dataclass(frozen=True)
class Part:
    kind: str
    type: int
    lba: int
    sectors: int


def parse_part(text: str) -> tuple[str, Path | None]:
    kind, sep, rest = text.partition(":")
    if kind not in TYPES:
        raise ValueError(f"unbekanntes Dateisystem {kind!r} (erlaubt: {', '.join(TYPES)})")
    return kind, Path(rest) if sep and rest else None


def _dir_bytes(path: Path | None) -> int:
    if path is None or not path.is_dir():
        return 0
    return sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def size_sectors(kind: str, content_bytes: int) -> int:
    """Partitionsgröße: Mindestgröße je Dateisystem, sonst doppelter Inhalt plus 16 MiB Reserve."""
    mib = max(MIN_MIB[kind], -(-content_bytes * 2 // (1024 * 1024)) + 16)
    return mib * 2048


def plan(sizes: list[tuple[str, int]]) -> list[Part]:
    if len(sizes) > 4:
        raise ValueError("höchstens vier primäre Partitionen")
    parts, lba = [], ALIGN
    for kind, sectors in sizes:
        sectors = -(-sectors // ALIGN) * ALIGN
        parts.append(Part(kind, TYPES[kind], lba, sectors))
        lba += sectors
    return parts


def build_mbr(parts: list[Part]) -> bytes:
    mbr = bytearray(512)
    for i, p in enumerate(parts):
        # CHS-Felder bleiben 0; libogc, libfat, libntfs und newlauncher lesen nur LBA.
        struct.pack_into("<B3sB3sII", mbr, 446 + 16 * i, 0, b"\0\0\0", p.type, b"\0\0\0", p.lba, p.sectors)
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def format_script(parts: list[Part], filled: list[bool]) -> str:
    """sh-Skript, das in $WORK aus in<i>/ die Images p<i>.img erzeugt."""
    lines = ["set -e", 'cd "$WORK"', "export MTOOLS_SKIP_CHECK=1"]
    for i, (p, fill) in enumerate(zip(parts, filled)):
        mib = p.sectors // 2048
        img = f"p{i}.img"
        lines.append(f"rm -f {img} && truncate -s {mib}M {img}")
        if p.kind == "fat32":
            lines.append(f"mkfs.fat -F 32 -S 512 -s 1 -h {p.lba} -n NLFAT{i} {img} >/dev/null")
            if fill:
                lines.append(f'if [ -n "$(ls -A in{i})" ]; then mcopy -s -Q -i {img} in{i}/* ::/; fi')
        elif p.kind == "ntfs":
            lines.append(f"mkntfs -F -Q -q -s 512 -c 4096 -p {p.lba} -H 0 -S 0 -L NLNTFS{i} {img}")
            if fill:
                lines.append(f"mkdir -p /mnt/p{i} && ntfs-3g {img} /mnt/p{i} && cp -r in{i}/. /mnt/p{i}/ && umount /mnt/p{i}")
        elif p.kind == "ext4":
            src = f"-d in{i}" if fill else ""
            lines.append(f"mkfs.ext4 -q -F -b 4096 -L nlext{i} {src} {img} {mib}M")
    return "\n".join(lines) + "\n"


def assemble(out: Path, parts: list[Part], work: Path) -> None:
    end = parts[-1].lba + parts[-1].sectors + ALIGN if parts else ALIGN
    with out.open("wb") as f:
        f.write(build_mbr(parts))
        for i, p in enumerate(parts):
            img = work / f"p{i}.img"
            if img.stat().st_size != p.sectors * 512:
                raise ValueError(f"{img.name}: {img.stat().st_size} Bytes statt {p.sectors * 512}")
            f.seek(p.lba * 512)
            with img.open("rb") as src:
                shutil.copyfileobj(src, f, 1024 * 1024)
        f.truncate(end * 512)


def _run_docker(work: Path) -> int:
    if subprocess.run(["docker", "image", "inspect", DOCKER_IMAGE], capture_output=True).returncode != 0:
        r = subprocess.run(["docker", "build", "-q", "-t", DOCKER_IMAGE, "-f", str(DOCKERFILE), str(DOCKERFILE.parent)])
        if r.returncode != 0:
            return r.returncode
    # FUSE nur für ntfs-3g (NTFS füllen); alles bleibt im Container und im Arbeitsordner.
    return subprocess.run(["docker", "run", "--rm", "--device", "/dev/fuse", "--cap-add", "SYS_ADMIN",
                           "--security-opt", "apparmor:unconfined", "-e", "WORK=/work",
                           "-v", f"{work}:/work", DOCKER_IMAGE, "sh", "/work/format.sh"]).returncode


def make_image(out: Path, specs: list[tuple[str, Path | None]], native: bool = False) -> int:
    parts = plan([(kind, size_sectors(kind, _dir_bytes(src))) for kind, src in specs])
    with tempfile.TemporaryDirectory(prefix="nl_disk_") as tmp:
        work = Path(tmp)
        filled = []
        for i, (_, src) in enumerate(specs):
            dst = work / f"in{i}"
            if src is not None and src.is_dir():
                shutil.copytree(src, dst)
            else:
                dst.mkdir()
            filled.append(src is not None)
        (work / "format.sh").write_text(format_script(parts, filled), encoding="utf-8", newline="\n")
        try:
            if native:
                rc = subprocess.run(["sh", str(work / "format.sh")], env={"WORK": str(work), "PATH": "/usr/sbin:/usr/bin:/sbin:/bin"}).returncode
            else:
                rc = _run_docker(work)
        except FileNotFoundError as e:
            print(f"DISK FEHLER: {e.filename} nicht gefunden (Docker installiert?)")
            return 2
        if rc != 0:
            print(f"DISK FEHLER: Formatieren scheiterte (rc={rc})")
            return 1
        out.parent.mkdir(parents=True, exist_ok=True)
        assemble(out, parts, work)
    print(f"{out}: " + ", ".join(f"{p.kind}@{p.lba}+{p.sectors}" for p in parts))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out", type=Path)
    ap.add_argument("--part", action="append", required=True, metavar="FS[:ORDNER]")
    ap.add_argument("--native", action="store_true", help="ohne Docker (Linux, Werkzeuge installiert, root)")
    args = ap.parse_args(argv)
    try:
        specs = [parse_part(t) for t in args.part]
        plan([(k, 1) for k, _ in specs])
    except ValueError as e:
        ap.error(str(e))
    if REPO_ROOT in args.out.resolve().parents:
        ap.error("das Image darf nicht im Repo liegen")
    return make_image(args.out, specs, args.native)


if __name__ == "__main__":
    sys.exit(main())
