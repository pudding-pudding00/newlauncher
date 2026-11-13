# Spike M16: NTFS und ext auf der Wii (Plan Task 1)

Stand: 2026-11-13. Frage: Lassen sich NTFS- und ext-Partitionen mit einer GPLv3-verträglichen Bibliothek **nur lesend**
einhängen, und liefert die Bibliothek die Extents einer Datei, damit der Launcher daraus eine Fragmentliste bauen kann?

**Ergebnis:** NTFS **positiv** (libntfs, 4 kleine Patches). ext **negativ** (Lizenz-Blocker, kein Mount moderner ext4,
Hänger im Mount). Die Probe-DOL lag nur im Scratchpad und wird nicht eingecheckt (sie baut Fremdcode, der vor dem
Vendoring in Task 2 nicht ins Repo gehört).

## 1. NTFS: libntfs (Wii-Port von NTFS-3G)

### Quelle

- Repo: `https://github.com/extremscorner/libntfs-wii`
- Commit: **`ae00fc8f22a5ba7d9db73755570687299a7dd64b`** („Handle EOVERFLOW in example“, 2024-02-15), Basis NTFS-3G 2022.10.3.
- Warum nicht HEAD (`417e8dd`): Die Commits ab `f91e02e` („Pass DISC_INTERFACE to interface“) und `2f2020e` (DMA-Prüfung
  über `SYS_IsDMAAddress`) zielen auf libogc2 und bauen nicht gegen das libogc aus devkitPro. Den einzigen späteren
  Fehlerfix (`417e8dd`, rekursiver Volume-Mutex) übernehmen wir als Patch (siehe unten).

### Lizenzen je Datei (Kopfzeilen von `source/` und `include/`)

| Dateien | Lizenz | Verträglich mit GPLv3 |
|---|---|---|
| 75 Dateien (NTFS-3G-Kern, `ntfs*.c`, `gekko_io.c`, `include/ntfs.h`) | GPLv2 „or (at your option) any later version“ | ja (GPLv3 gewählt) |
| `cache2.c`, `cache2.h`, `bit_ops.h` | BSD-artig (aus libfat, Chism) | ja |
| `realpath.c`, `realpath.h` | **kein Kopf** („Originated from the util-linux project“) | ohne Beleg → **nicht vendort** |
| `config.h` | generiert, keine Schöpfungshöhe; Repo-`LICENSE` (GPLv2) als Beleg | ja |

`realpath.c/h` wird nur in `volume.c` in Zweigen unter `#ifdef HAVE_MNTENT_H` bzw. `__sun && __SVR4` benutzt; beide
sind auf der Wii nicht definiert (`config.h`: `HAVE_MNTENT_H` undefiniert). Task 2 lässt die Datei weg und entfernt das
`#include "realpath.h"` aus `volume.c` (Patch 4).

### Mount (nur lesend)

```c
bool ntfsMount(const char* name, const DISC_INTERFACE* interface, sec_t startSector,
               u32 cachePageCount, u32 cachePageSize, u32 flags);
// Aufruf im Probe: ntfsMount("ntfs", &__io_wiisd, partLba, 8, 64, NTFS_READ_ONLY | NTFS_SHOW_HIDDEN_FILES)
```

- `NTFS_READ_ONLY` (0x20) setzt `NTFS_MNT_RDONLY`; die Gerätschicht `gekko_io.c` lehnt dann jedes Schreiben mit
  `EROFS` ab.
- **Nie `NTFS_RECOVER`** übergeben: Das setzt `$LogFile` zurück, also ein Schreibzugriff.
- `startSector` zählt in **Gerätesektoren**. `gekko_io` übernimmt `bytes_per_sector` aus dem NTFS-Bootsektor als
  Sektorgröße des Geräts. Bei 4-KiB-Platten sind `startSector` und alle Geräte-LBAs deshalb 4-KiB-Einheiten → Task 3
  rechnet in `core/extent_units` ausdrücklich um (Review Focus 5).

### Nötige Patches (liegen ab Task 2 unter `third_party/libntfs/patches/`)

1. **Rekursiver Volume-Mutex** (`ntfsinternal.c`: `LWP_MutexInit(&vd->lock, true)`). libogc-Mutexe sind ohne
   `use_recursive` nicht wiedereintrittsfähig; libntfs sperrt beim Lesen verschachtelt → `fread` hängt endlos.
   Upstream hat das in `417e8dd` genauso behoben. Im Probe nachgewiesen: ohne Patch Hänger nach „mounted“, mit Patch
   liest `fread` die 8 MiB vollständig.
2. **Schreib-Opens auf RO-Volumes ablehnen** (`ntfsfile.c`, `ntfs_open_r`): Ohne Patch liefert `fopen(..., "wb")`
   auf einem RO-Volume ein Handle (die Datei entsteht nur im Speicher; das Gerät blieb im Probe byte-identisch). Mit
   Patch kommt sofort `EROFS`. Verteidigung in der Tiefe, damit kein Launcher-Pfad „erfolgreich“ auf NTFS schreibt.
3. **Build-Kompatibilität mit devkitPro-libogc:** `-include gcbool.h` (Typ `BOOL`) und ein `makedev`-Ersatz für
   `ea.c` (`((ma & 0xffff) << 16) | (mi & 0xffff)`, nur für Gerätenummern in erweiterten Attributen).
4. **`realpath` entfernen** (siehe Lizenzen).

### Warnungen (devkitPPC, `-Wall -Wextra`)

18 Warnungen, alle in Fremdcode: 11× `-Wformat` (`%lld` für `sec_t`, das in libogc `u32` ist; `gekko_io.c`, `ntfs.c`),
4× `-Wzero-length-bounds` (`reparse.c`, `ea.c` je 2), 2× `-Wunused-but-set-variable` (`attrib.c`), 1×
`-Wuse-after-free` (`inode.c`, `realloc`-Muster). Task 2 behebt die `-Wformat`-Fälle per Patch (Casts) und baut das
Fremd-Target mit gezielten `-Wno-…` für die übrigen Klassen, damit der Wii-Build warnungsfrei bleibt.

### Extents

```c
ntfs_inode* ni = ntfs_pathname_to_inode(vol, NULL, "/pfad");
ntfs_attr*  na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
ntfs_attr_map_whole_runlist(na);          // füllt na->rl
for (runlist_element* rl = na->rl; rl->length; ++rl) { rl->vcn, rl->lcn, rl->length }  // Einheit: Cluster
```

- Einheit Cluster (`vol->cluster_size` Bytes); `lcn < 0` (`LCN_HOLE` usw.) = Lücke.
- Nicht unterstützt (→ `Unsupported`, Review Focus 1):
  - resident: `!NAttrNonResident(na)` (Datei steckt in der MFT);
  - komprimiert / sparse / verschlüsselt: `na->data_flags & (ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE | ATTR_IS_ENCRYPTED)`;
  - nicht initialisierter Rest: `na->initialized_size < na->data_size`;
  - irgendein Runlist-Element mit `lcn < 0`.
- Umrechnung: `lba512 = partLba512 + lcn × cluster_size / 512`, `fileSector = vcn × cluster_size / 512`,
  `count = length × cluster_size / 512`.

### Probe-Ergebnis (Dolphin, SD-Image mit MBR, NTFS-Partition ab LBA 2048, 64 MiB, Cluster 4096)

```
NL_FS_PROBE ntfs mounted
NL_FS_PROBE ntfs fsread=1
NL_FS_PROBE ntfs vol cluster=4096 sector=512
NL_FS_PROBE ntfs extent 0 file=0 lba=71680 count=16384
NL_FS_PROBE ntfs ok extents=1
```

- Lesen über `fopen/fread` und Rohlesen über die Extents liefern beide das bekannte 8-MiB-Muster.
- Der Schreibversuch scheitert (mit Patch 2).
- Das SD-Image ist nach dem Lauf byte-identisch zum Original (`cmp`).

**Entscheidung NTFS: positiv.** Vendoren in Task 2 mit Commit `ae00fc8` und Patches 1–4.

## 2. ext2/3/4: libext2fs-wii

### Quelle

- `svn://svn.code.sf.net/p/libext2fs-wii/code/trunk`, Revision **r21** (2012-07-08, Dimok), Basis e2fsprogs 1.42.4.
  Andere Wii-Ports (WiiFlow Lite, USB Loader GX) führen denselben Stand als Kopie mit.

### Lizenzen je Datei

| Dateien | Lizenz | Verträglich |
|---|---|---|
| 71 | „GNU Library General Public License, version 2“ | ja (LGPLv2 §3 erlaubt GPLv2+/v3) |
| 15 | GPL/LGPL „or later“ | ja |
| 3 | BSD-artig | ja |
| mehrere (`blkmap64_ba.c`, `blknum.c`, `mmp.c`, `gen_bitmap64.c` …) | „GNU Public License“ ohne Version | ja (beliebige Version wählbar) |
| **`crc16.c`, `crc16.h`, `crc32c.c`** | **GPLv2 only** (Linux-Kernel) | **nein** |
| 28 | **kein Kopf** (u. a. `jfs_user.h` aus e2fsck = GPLv2, `tdb.c`, `qcow2.c`, `progress.c`) | ohne Beleg nein |

### Bauen

Baut nach 3 Patches: devoptab-Signatur (`int fd` → `void* fd` in `ext2_*_r`), Endlosrekursion in
`ext2fs_adjust_ea_refcount` (ruft sich selbst statt `…2`), `-std=gnu17 -fgnu89-inline` (GCC 15 macht `bool` zum
Schlüsselwort und ändert `extern inline`). Rund 15 Warnungen, plus Makro-Neudefinitionen in `config.h`/`ext2fs.h`.
Der rekursive Mutex (wie NTFS-Patch 1) wurde ebenfalls nachgezogen.

### Probe-Ergebnis

- Image mit dem heutigen Standard von `mkfs.ext4` (e2fsprogs 1.47, Features u. a. `metadata_csum_seed`,
  `orphan_file`): `NL_FS_PROBE ext fail mount`. `metadata_csum_seed` ist ein INCOMPAT-Feature, das 1.42.4 nicht kennt.
- Image ohne diese beiden Features (`-O ^metadata_csum_seed,^orphan_file`): `ext2Mount` kehrt nicht zurück
  (Timeout nach 120 s, auch mit rekursivem Mutex). Das Image blieb unverändert.
- Extent-API wäre vorhanden (`ext2fs_extent_open2` / `ext2fs_extent_get(…, EXT2_EXTENT_NEXT_LEAF, …)`,
  `EXT2_EXTENT_FLAGS_UNINIT`; ext2/3 über `ext2fs_bmap2`), kam aber wegen des Mounts nicht zum Einsatz.

**Entscheidung ext: negativ.** Gründe:

1. Lizenz: `crc16`/`crc32c` sind GPLv2-only. Eigene Ersatzimplementierungen wären machbar, dazu kommen aber 28 Dateien
   ohne Lizenzkopf.
2. Moderne ext4-Platten (Standard-`mkfs.ext4` seit e2fsprogs 1.47) werden abgelehnt. Für die meisten Nutzer
   funktioniert der Mount also gar nicht.
3. Selbst mit älterem Feature-Set hängt der Mount in Dolphin.
4. Unbetreut seit 2012. Eine Portierung eines aktuellen e2fsprogs wäre ein eigener Meilenstein.

Folge für M16: `FsKind::Ext` bleibt `mount=false` mit Grund „ext wird nicht unterstützt“ / „ext is not supported“.
`NL_FS_EXT` entfällt. Die ext-Teile von Task 2–4 (Mount, Extents, `fs = ext` im Profil-Abgleich) entfallen; die
Systeminfo zeigt ext-Partitionen als „nicht eingehängt“ mit diesem Grund.

## 3. Was Task 2–4 daraus übernehmen

- Nur `third_party/libntfs/` (Commit `ae00fc8`, ohne `realpath.c/h`, Patches 1–4, `VENDORED.txt`, `THIRD_PARTY.md`,
  `check_licenses.py`).
- Mount in `DeviceTable::mountUsb()` je Slot mit `NTFS_READ_ONLY | NTFS_SHOW_HIDDEN_FILES`, `Device::writable = false`.
- `platform/fs_extents` kapselt `ntfs_attr_map_whole_runlist`; `core/extent_units` rechnet Cluster → 512-Byte-Sektoren
  und berücksichtigt die Gerätesektorgröße (4 KiB).
