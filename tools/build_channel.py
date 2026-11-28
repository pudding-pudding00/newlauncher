"""Local Wii / vWii channel (UNLA, 00010001-554E4C41) asset & staging tool (M30).

Generates 100% original IMET banner header (00000000.app) and local Wii/vWii WAD staging
metadata without ever committing WADs, third-party Nintendo banners, or the Wii Common Key.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

TITLE_ID_STR = "00010001554E4C41"
TITLE_FOURCC = "UNLA"
IMET_SIZE = 0x600


def build_imet_header(title: str = "newlauncher") -> bytes:
    """Build a valid 0x600-byte IMET banner header with UTF-16BE localized titles and MD5."""
    buf = bytearray(IMET_SIZE)
    buf[0x40:0x44] = b"IMET"
    struct.pack_into(">III", buf, 0x44, 0x00000600, 0x00000003, 0)
    encoded = title.encode("utf-16-be")[:82]
    # 10 language slots, 84 bytes each, starting at 0x5C.
    for lang_idx in range(10):
        off = 0x5C + lang_idx * 84
        buf[off:off + len(encoded)] = encoded
    # MD5 over bytes [0x40..0x600) with the MD5 field at [0x5F0..0x600) zeroed.
    digest = hashlib.md5(bytes(buf[0x40:0x600])).digest()
    buf[0x5F0:0x600] = digest
    return bytes(buf)


def verify_imet_header(data: bytes) -> bool:
    if len(data) < IMET_SIZE or data[0x40:0x44] != b"IMET":
        return False
    want = data[0x5F0:0x600]
    tmp = bytearray(data[0x40:0x600])
    tmp[0x5F0 - 0x40:0x600 - 0x40] = b"\x00" * 16
    return hashlib.md5(bytes(tmp)).digest() == want


def stage_channel(target: str, dol_path: Path, out_dir: Path, title: str = "newlauncher") -> Path:
    """Stage UNLA channel contents (00000000.app + forwarder.dol + manifest.json) locally."""
    if target not in ("wii", "vwii"):
        raise ValueError(f"unsupported target: {target} (expected 'wii' or 'vwii')")
    if not dol_path.is_file():
        raise FileNotFoundError(f"forwarder DOL not found: {dol_path}")
    dol_bytes = dol_path.read_bytes()
    if len(dol_bytes) < 0x100:
        raise ValueError(f"invalid DOL (too small): {dol_path}")

    out_dir.mkdir(parents=True, exist_ok=True)
    imet = build_imet_header(title)
    (out_dir / "00000000.app").write_bytes(imet)
    (out_dir / "forwarder.dol").write_bytes(dol_bytes)
    manifest = {
        "title_id": TITLE_ID_STR,
        "fourcc": TITLE_FOURCC,
        "target": target,
        "title": title,
        "dol_sha256": hashlib.sha256(dol_bytes).hexdigest(),
        "imet_md5": imet[0x5F0:0x600].hex(),
        "warning": "Verify console target (Wii vs vWii) and BootMii NAND backup before installing any WAD.",
    }
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--target", choices=["wii", "vwii"], required=True)
    p.add_argument("--dol", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--title", default="newlauncher")
    args = p.parse_args(argv)
    try:
        manifest = stage_channel(args.target, args.dol, args.out, args.title)
    except (OSError, ValueError) as e:
        print(f"build_channel error: {e}", file=sys.stderr)
        return 2
    print(f"Staged UNLA ({args.target}) channel -> {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
