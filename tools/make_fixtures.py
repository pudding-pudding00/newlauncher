"""Erzeugt die Fixture-Bibliothek für Dolphin-Tests (Spec 8.3): nur Dummy-Header und leere ROM-Dateien.

    python tools/make_fixtures.py tests/fixtures/sd
"""

import struct
import sys
from pathlib import Path

from metabin import write_meta_bin

WII_MAGIC = 0x5D1C9EA3
GC_MAGIC = 0xC2339F3D

CONFIG_INI = (
    "[general]\nlanguage = auto\nlibrary = sd\nusb_timeout = 15\n"
    "[theme]\nmode = light\ndark_from = 19:00\ndark_to = 07:00\n"
    "[audio]\nmusic = 1\nmusic_volume = 60\nsounds = 1\n"
    "[covers]\nregion = auto\n"
)

# Alle GX-Konsolen starten die Test-DOL argv_echo; sie meldet die übergebenen argv im Dolphin-Log.
EMULATORS_INI = "".join(
    f"[{c}]\ndol = sd:/apps/argv_echo/boot.dol\nargs = {{dir}}|{{file}}\n" for c in ["nes", "snes", "gb", "gbc", "gba"]
) + "[n64_not64]\ndol = sd:/apps/argv_echo/boot.dol\nargs = {path}\n"  # [n64_wii64] bleibt Standard: DOL fehlt

# Mario Kart 64 läuft über den Testkanal aus tools/wii_title.py (VC-Weg), Super Mario 64 über Not64.
N64_VC_INI = "[map]\nn64/Mario Kart 64 (Europe).z64 = 000100014E4C5443\n"

EMPTY_ROMS = [
    "roms/nes/Super Mario Bros. (World).nes",
    "roms/nes/Zelda (Europe) (Rev 1).nes",
    "roms/nes/1942 (Japan).nes",
    "roms/snes/Super Metroid (Europe) (En,Fr,De).sfc",
    "roms/snes/F-Zero (Europe).SMC",
    "roms/gb/Tetris (World) (Rev 1).gb",
    "roms/n64/Super Mario 64 (Europe) (En,Fr,De).z64",
    "roms/n64/Mario Kart 64 (Europe).z64",
    "roms/gba/Metroid Fusion (Europe).gba",
]


def disc_header(id6: str, title: str, wii: bool) -> bytearray:
    data = bytearray(1024)
    data[0:6] = id6.encode("ascii")
    if wii:
        struct.pack_into(">I", data, 0x18, WII_MAGIC)
    else:
        struct.pack_into(">I", data, 0x1C, GC_MAGIC)
    data[0x20:0x20 + len(title)] = title.encode("latin-1")
    return data


def wbfs_file(id6: str, title: str) -> bytes:
    data = bytearray(1024)
    data[0:4] = b"WBFS"
    struct.pack_into(">I", data, 4, 1)
    data[8] = 9   # hd_sec_sz_s: 512-Byte-Sektoren
    data[9] = 21  # wbfs_sec_sz_s
    data[512:1024] = disc_header(id6, title, wii=True)[:512]
    return bytes(data)


def write(out: Path, rel: str, data: bytes | str) -> None:
    path = out / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data, encoding="utf-8", newline="\n")
    else:
        path.write_bytes(data)


def main(out: Path) -> int:
    write(out, "newlauncher/config.ini", CONFIG_INI)
    write(out, "newlauncher/emulators.ini", EMULATORS_INI)
    write(out, "newlauncher/n64_vc.ini", N64_VC_INI)
    write(out, "newlauncher/favorites.ini",
          "[favorites]\nRMCP01\nnes/Super Mario Bros. (World).nes\nn64/gibts nicht.z64\n")
    write(out, "newlauncher/history.ini",
          "[history]\n1762700000 = n64/Super Mario 64 (Europe) (En,Fr,De).z64\n")
    write_meta_bin(out / "newlauncher/cache/meta.bin", {
        "RMCP01": ("Mario Kart Wii", 2008, 4, "Rennspiel mit Lenkrad-Steuerung und Online-Modus."),
        "n64/super mario 64 (europe) (en,fr,de)": ("Super Mario 64", 1996, 1,
                                                    "Mario sucht im Schloss nach Power-Sternen."),
    })
    for rel in EMPTY_ROMS:
        write(out, rel, b"")
    write(out, "roms/nes/readme.txt", "Keine ROM – wird vom Scanner ignoriert.\n")
    write(out, "roms/gbc/.keep", b"")
    write(out, "games/Metroid Prime [GM8P01]/game.iso", bytes(disc_header("GM8P01", "Metroid Prime", wii=False)))
    write(out, "wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs", wbfs_file("RMCP01", "MARIO KART WII"))
    write(out, "wbfs/Super Mario Galaxy [RMGP01]/RMGP01.iso",
          bytes(disc_header("RMGP01", "Super Mario Galaxy", wii=True)))
    write(out, "wbfs/Kaputt [XXXX01]/XXXX01.wbfs", bytes([0xAB]) * 1024)
    print(f"Fixtures geschrieben nach {out}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(Path(sys.argv[1])))
