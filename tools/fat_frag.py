"""Unabhängiger Abgleich für M9: erwartete NL_FRAG-Zeile einer ISO aus einem FAT32-Image.

Liest MBR/BPB, Verzeichnisse (mit langen Namen) und die FAT-Kette selbst, ohne den C++-Code. Die Liste wird
wie im Launcher verschmolzen und serialisiert (240 012 Byte, Big-Endian), die CRC ist zlib.crc32.
Benutzt vom Dolphin-Smoke-Test (--expect-frag), der so libfats st_ino-Annahme gegenprüft.
"""

import struct
import zlib
from pathlib import Path

SECTOR = 512
MAX_FRAG = 20000
EOC = 0x0FFFFFF8


class NotFat32Error(Exception):
    """Das Image enthält kein FAT32-Volume mit 512-Byte-Sektoren."""


class _Image:
    def __init__(self, path: Path):
        self.f = path.open("rb")

    def read(self, lba: int, count: int = 1) -> bytes:
        self.f.seek(lba * SECTOR)
        data = self.f.read(count * SECTOR)
        return data + bytes(count * SECTOR - len(data))

    def close(self) -> None:
        self.f.close()


def _is_fat_boot(s: bytes) -> bool:
    if s[510:512] != b"\x55\xAA" or struct.unpack_from("<H", s, 0x0B)[0] != SECTOR:
        return False
    spc = s[0x0D]
    if spc == 0 or spc & (spc - 1):
        return False
    if s[0x10] == 0 or struct.unpack_from("<H", s, 0x0E)[0] == 0:
        return False
    return struct.unpack_from("<H", s, 0x13)[0] != 0 or struct.unpack_from("<I", s, 0x20)[0] != 0


class _Volume:
    def __init__(self, img: _Image, lba: int, s: bytes):
        self.img = img
        self.part_lba = lba
        reserved = struct.unpack_from("<H", s, 0x0E)[0]
        fats = s[0x10]
        root_dir_sectors = (struct.unpack_from("<H", s, 0x11)[0] * 32 + SECTOR - 1) // SECTOR
        fat_sz = struct.unpack_from("<H", s, 0x16)[0] or struct.unpack_from("<I", s, 0x24)[0]
        total = struct.unpack_from("<H", s, 0x13)[0] or struct.unpack_from("<I", s, 0x20)[0]
        meta = reserved + fats * fat_sz + root_dir_sectors
        if fat_sz == 0 or meta >= total:
            raise NotFat32Error("BPB ergibt keine Geometrie")
        self.spc = s[0x0D]
        self.fat_lba = lba + reserved
        self.data_lba = lba + meta
        self.cluster_count = (total - meta) // self.spc
        if self.cluster_count < 65525:
            raise NotFat32Error(f"{self.cluster_count} Cluster: FAT12/16")
        self.root_cluster = struct.unpack_from("<I", s, 0x2C)[0]

    def cluster_lba(self, c: int) -> int:
        return self.data_lba + (c - 2) * self.spc

    def next_cluster(self, c: int) -> int:
        sec = self.img.read(self.fat_lba + c * 4 // SECTOR)
        return struct.unpack_from("<I", sec, c * 4 % SECTOR)[0] & 0x0FFFFFFF

    def chain(self, start: int, limit: int | None = None) -> list[int]:
        out, c = [], start
        while 2 <= c < self.cluster_count + 2 and len(out) <= self.cluster_count:
            out.append(c)
            if limit is not None and len(out) == limit:
                break
            c = self.next_cluster(c)
        return out

    def list_dir(self, cluster: int) -> dict[str, tuple[int, int, bool]]:
        """Name (klein) → (Startcluster, Größe, ist Ordner)."""
        entries: dict[str, tuple[int, int, bool]] = {}
        lfn: dict[int, str] = {}
        for c in self.chain(cluster):
            data = self.img.read(self.cluster_lba(c), self.spc)
            for off in range(0, len(data), 32):
                e = data[off:off + 32]
                if e[0] == 0:
                    return entries
                if e[0] == 0xE5:
                    lfn.clear()
                    continue
                if e[11] == 0x0F:
                    units = struct.unpack_from("<5H", e, 1) + struct.unpack_from("<6H", e, 14) + struct.unpack_from("<2H", e, 28)
                    lfn[e[0] & 0x1F] = "".join(chr(u) for u in units if u not in (0, 0xFFFF))
                    continue
                if lfn:
                    name = "".join(lfn[k] for k in sorted(lfn))
                else:
                    base, ext = e[0:8].decode("latin-1").rstrip(), e[8:11].decode("latin-1").rstrip()
                    name = base + ("." + ext if ext else "")
                lfn.clear()
                if e[11] & 0x08:  # Volume-Label
                    continue
                start = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
                entries[name.lower()] = (start, struct.unpack_from("<I", e, 28)[0], bool(e[11] & 0x10))
        return entries


def _find_volume(img: _Image) -> _Volume:
    s = img.read(0)
    if _is_fat_boot(s):
        return _Volume(img, 0, s)
    if s[510:512] != b"\x55\xAA":
        raise NotFat32Error("weder Bootsektor noch MBR")
    for i in range(4):
        e = s[0x1BE + 16 * i:0x1BE + 16 * (i + 1)]
        lba = struct.unpack_from("<I", e, 8)[0] if e[4] else 0
        if lba and _is_fat_boot(b := img.read(lba)):
            return _Volume(img, lba, b)
    raise NotFat32Error("kein FAT-Volume im MBR")


def _merge(entries: list[list[int]], off: int, lba: int, count: int) -> None:
    if entries and entries[-1][0] + entries[-1][2] == off and entries[-1][1] + entries[-1][2] == lba:
        entries[-1][2] += count
    else:
        entries.append([off, lba, count])


def expected_frag_line(image: Path, rel_path: str) -> str:
    """NL_FRAG-Zeile für eine ISO unter rel_path (relativ zur Wurzel des Volumes)."""
    if not rel_path.lower().endswith(".iso"):
        raise ValueError("nur .iso wird abgeglichen")
    img = _Image(image)
    try:
        vol = _find_volume(img)
        cluster, size, is_dir = vol.root_cluster, 0, True
        for part in [p for p in rel_path.split("/") if p]:
            if not is_dir:
                raise FileNotFoundError(rel_path)
            found = vol.list_dir(cluster).get(part.lower())
            if found is None:
                raise FileNotFoundError(rel_path)
            cluster, size, is_dir = found
        sectors = (size + SECTOR - 1) // SECTOR
        need = (sectors + vol.spc - 1) // vol.spc
        clusters = vol.chain(cluster, need)
        if len(clusters) < need:
            raise ValueError("Clusterkette zu kurz")
        entries: list[list[int]] = []
        left = sectors
        for i, c in enumerate(clusters):
            n = min(vol.spc, left)
            _merge(entries, i * vol.spc, vol.cluster_lba(c), n)
            left -= n
    finally:
        img.close()
    if len(entries) > MAX_FRAG:
        raise ValueError("mehr als 20000 Fragmente")
    total = entries[-1][0] + entries[-1][2] if entries else 0
    blob = bytearray(12 + MAX_FRAG * 12)
    struct.pack_into(">III", blob, 0, total, len(entries), MAX_FRAG)
    for i, e in enumerate(entries):
        struct.pack_into(">III", blob, 12 + 12 * i, *e)
    first = f"{entries[0][0]}:{entries[0][1]}:{entries[0][2]}" if entries else "-"
    return (f"NL_FRAG dev=sd part_lba={vol.part_lba} n={len(entries)} size={total} "
            f"crc={zlib.crc32(blob):08x} first={first}")
