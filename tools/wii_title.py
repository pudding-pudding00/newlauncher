"""Legt einen Test-Kanal im Dolphin-NAND an (M4): TMD, Ticket, IMET-Banner und Boot-DOL als Content.

Nur so viel Struktur, wie ES_GetTitles/ES_GetStoredTMD und ISFS brauchen; Signaturen bleiben Nullen
(Dolphin prüft sie nicht). Kein Nintendo-Inhalt: der Boot-Content ist die eigene Test-DOL argv_echo.
"""

import hashlib
import struct
from pathlib import Path

SIG_RSA2048 = 0x00010001
TMD_HEADER = 0x1E4  # Contents beginnen hier, je 36 Byte
TICKET_SIZE = 0x2A4
IMET_SIZE = 0x600
IMET_NAMES = 0x5C
IMET_NAME_UNITS = 42
IMET_LANGUAGES = 10

TEST_CHANNEL_ID = 0x000100014E4C5443  # 00010001-"NLTC"
TEST_CHANNEL_NAMES = {1: "NL Testkanal", 2: "NL Testkanal"}


def tmd_bytes(title_id: int, contents: list[bytes], boot_index: int) -> bytes:
    """TMD wie in core/wii_title.hpp: Title-ID 0x18C, Anzahl 0x1DE, Boot-Index 0x1E0, Contents ab 0x1E4."""
    out = bytearray(TMD_HEADER + 36 * len(contents))
    struct.pack_into(">I", out, 0, SIG_RSA2048)
    out[0x140:0x140 + 26] = b"Root-CA00000001-CP00000004"
    struct.pack_into(">Q", out, 0x18C, title_id)
    struct.pack_into(">I", out, 0x194, 1)  # title_type
    struct.pack_into(">H", out, 0x1DE, len(contents))
    struct.pack_into(">H", out, 0x1E0, boot_index)
    for i, data in enumerate(contents):
        at = TMD_HEADER + 36 * i
        struct.pack_into(">IHHQ", out, at, i, i, 1, len(data))
        out[at + 16:at + 36] = hashlib.sha1(data).digest()
    return bytes(out)


def ticket_bytes(title_id: int) -> bytes:
    out = bytearray(TICKET_SIZE)
    struct.pack_into(">I", out, 0, SIG_RSA2048)
    out[0x140:0x140 + 26] = b"Root-CA00000001-XS00000003"
    struct.pack_into(">Q", out, 0x1DC, title_id)
    return bytes(out)


def imet_banner(names: dict[int, str]) -> bytes:
    """IMET-Kopf: Magic bei 0x40, Namen UTF-16BE ab 0x5C, 42 Einheiten je Sprache (Index wie BannerLang)."""
    out = bytearray(IMET_SIZE)
    out[0x40:0x44] = b"IMET"
    struct.pack_into(">II", out, 0x44, IMET_SIZE, 3)
    for lang, name in names.items():
        if not 0 <= lang < IMET_LANGUAGES:
            raise ValueError(f"Sprache {lang} außerhalb 0..{IMET_LANGUAGES - 1}")
        encoded = name.encode("utf-16-be")
        if len(encoded) > IMET_NAME_UNITS * 2:
            raise ValueError(f"Name länger als {IMET_NAME_UNITS} UTF-16-Einheiten: {name!r}")
        at = IMET_NAMES + lang * IMET_NAME_UNITS * 2
        out[at:at + len(encoded)] = encoded
    return bytes(out)


def install_channel(wii_root: Path, title_id: int, names: dict[int, str], boot_dol: bytes) -> None:
    """Schreibt Titel und Ticket unter wii_root (Dolphin: <user>/Wii). Content 0 Banner, 1 Boot-DOL."""
    hi, lo = f"{title_id >> 32:08x}", f"{title_id & 0xFFFFFFFF:08x}"
    base = wii_root / "title" / hi / lo
    content = base / "content"
    content.mkdir(parents=True, exist_ok=True)
    (base / "data").mkdir(exist_ok=True)
    contents = [imet_banner(names), boot_dol]
    for i, data in enumerate(contents):
        (content / f"{i:08x}.app").write_bytes(data)
    (content / "title.tmd").write_bytes(tmd_bytes(title_id, contents, boot_index=1))
    ticket_dir = wii_root / "ticket" / hi
    ticket_dir.mkdir(parents=True, exist_ok=True)
    (ticket_dir / f"{lo}.tik").write_bytes(ticket_bytes(title_id))
