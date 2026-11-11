# M16 Dateisysteme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:**
- NTFS- und ext2/3/4-Partitionen nur lesend einhängen und Spiele davon starten, soweit der Spike das trägt.
- WBFS-Partitionen erkennen und Wii-Spiele daraus starten.

**Architecture:**
- M12 hat das Gerätemodell geliefert: `PartitionInfo`/`FsKind`, `DeviceSlot`, `DeviceTable`. M16 schaltet dort `Ntfs`, `Ext` und `Wbfs` frei.
- Die Fragmentliste für Wii-Spiele entsteht weiter aus `FilePart` (`core/frag_list`). Neu ist nur, woher die Extents kommen:
  - FAT: `fileExtents` (wie bisher);
  - NTFS/ext: Extent-Abfrage der jeweiligen Bibliothek, gekapselt in `platform/fs_extents`;
  - WBFS-Partition: ein einziges Extent ab Partitions-LBA.
- Die Gegenprobe aus M9 (Stichproben-Sektoren über das Dateisystem gegen das Rohgerät lesen) gilt für alle Wege.

**Tech Stack:** C++20, devkitPPC/libogc, libfat, libntfs und libext2fs (Wii-Ports, vendort nach Spike), doctest, Python-Hilfsskripte.

**Spec:** `docs/superpowers/specs/2026-11-11-v2-universell-design.md` – 3.3, 5.2, 9.1–9.3, 13, 15.

**Basis-Branch:** `feat/m16-dateisysteme` von `main` nach M12. M16 ist von M13–M15 unabhängig, setzt aber deren Änderungen voraus, falls sie schon gemergt sind.

## Global Constraints

- **Nur lesend.**
  - NTFS und ext werden ohne Schreibrechte eingehängt (Mount-Flags der Bibliothek, Step im Spike).
  - Auf WBFS-Partitionen schreibt der Launcher nie.
  - `Device::writable == false` für all diese Geräte; Caches liegen unter `<data>/cache/<gerät>/` (M12 Task 6).
- **Lizenz:** Jede vendorte Datei ist GPLv3-verträglich nach Spec 3.3, steht in `THIRD_PARTY.md` und wird von `check_licenses.py` geprüft. Eine Bibliothek ohne belegte Lizenz entfällt.
- **Fällt eine Bibliothek im Spike durch:**
  - Ihr `FsKind` bleibt `mount=false` mit Grund „NTFS wird nicht unterstützt“ bzw. „ext wird nicht unterstützt“ (DE/EN).
  - Die Systeminfo (M13) zeigt die Partition als „nicht eingehängt“ mit diesem Grund.
- **Ohne NTFS/ext/WBFS-Partition** ist das Verhalten byte-gleich zu M12: dieselben Gerätenamen, dieselben Marker, dieselbe Fragmentliste.
- **Build-Schalter** `NL_FS_NTFS` und `NL_FS_EXT` (Standard ON, wenn der Spike positiv ist). Der Wii-Build ist in jeder Kombination warnungsfrei; CI baut ON/ON und OFF/OFF.
- Texte DE/EN; alle früheren Smokes grün.

## Review Focus

1. NTFS-Datei, die komprimiert, sparse oder resident (in der MFT) gespeichert ist: Die Extent-Abfrage meldet `Unsupported`. Der Preflight sagt „Datei ist komprimiert oder lückenhaft – bitte unkomprimiert kopieren“; nie eine falsche Fragmentliste.
2. ext4-Datei mit Extent-Baum über mehrere Ebenen oder mit `uninit`-Extents: Die Extents werden vollständig gelesen; `uninit` gilt als Lücke, also `Unsupported`.
3. WBFS-Partition, deren Kopf mehr Disc-Slots meldet, als in den ersten Sektor passen, oder Slots mit kaputtem Disc-Kopf: Die betroffenen Slots werden übersprungen und gezählt (`NL_WBFS_SKIP <n>`); die übrigen erscheinen.
4. Dieselbe ID6 auf einer WBFS-Partition und als Datei auf `usb`: Beide erscheinen nach den Duplikatregeln aus M12 (`alsoOn`), gestartet wird die Quelle mit Vorrang.
5. Platte mit 4-KiB-Sektoren und NTFS: Die Extents werden in 512-Byte-Sektoren der Fragmentliste umgerechnet; die Gegenprobe liest an der richtigen Stelle.

---

### Task 1: Spike NTFS und ext

**Files:**
- Create: `docs/superpowers/spikes/m16-dateisysteme.md`, `tools/spikes/m16_fs_probe/` (Wegwerf-DOL, wird nicht gemergt; nur das Spike-Dokument bleibt)

- [ ] **Step 1: Quellen sichten.** Die Wii-Ports von libntfs und libext2fs (devkitPro-Umfeld, Repos von WiiFlow Lite/USB Loader GX). Je Bibliothek ins Spike-Dokument:
  - Repo, Commit, Lizenz **je Datei** (Kopfzeilen; Dateien ohne Kopf bekommen die Repo-Lizenz nur mit Beleg);
  - ob alle Dateien GPLv2-oder-später, GPLv3 oder permissiv sind (Spec 3.3);
  - Mount-Aufruf mit Nur-Lese-Flag und seine Signatur (Startsektor, Cache-Seiten).
- [ ] **Step 2: Bauen.** Mit dem devkitPPC aus CI bauen, Warnungen zählen. Notieren, welche Patches nötig sind; Patches liegen später unter `third_party/<lib>/patches/` mit Begründung.
- [ ] **Step 3: Extents.** Je Bibliothek die Funktion finden, die einer Datei ihre Laufliste gibt:
  - NTFS: Runlist der `$DATA`-Attribute;
  - ext: Extent-Baum (ext4) bzw. Blockliste (ext2/3).
  - Notieren: Name, Signatur, Einheiten (Cluster/Blöcke), wie komprimierte, sparse und residente Dateien erkennbar sind.
- [ ] **Step 4: Probe-DOL** (nur Dolphin, kein Hardware-Zwang). Ein 64-MiB-Image je Dateisystem, erzeugt mit `mkntfs`/`mkfs.ext4` (lokal über Docker Desktop in einem `debian:stable-slim`-Container, da die WSL nur `docker-desktop` hat), darin eine 8-MiB-Datei mit bekanntem Muster. Das DOL hängt ein, liest die Datei über das Dateisystem und über die Extents vom Rohgerät, vergleicht. Ausgabe `NL_FS_PROBE ntfs ok extents=<n>` bzw. `ext4 ok`.
- [ ] **Step 5: Entscheidung** je Bibliothek ins Spike-Dokument: positiv (Commit, Lizenz, Patches) oder negativ (Grund). Bei negativem Ausgang entfallen die betroffenen Teile von Task 2–4; der Ledger hält das als Ruling fest.
- [ ] **Step 6: Commit** `docs: Spike NTFS und ext`

### Task 2: Vendoring und nur lesendes Einhängen

**Files:**
- Create:
  - `third_party/libntfs/`, `third_party/libext2fs/` (je mit `VENDORED.txt`: Repo, Commit, Lizenz, Patches);
  - `tools/vendor_fs.py` (holt den Commit aus dem Spike, wendet `patches/` an, prüft Hashes), `tools/tests/test_vendor_fs.py`;
  - `tools/make_disk_image.py`: baut ein Platten-Image mit MBR und den Partitionen `fat32|ntfs|ext4|wbfs` in gegebener Reihenfolge. FAT32 und WBFS schreibt das Skript selbst (WBFS aus `make_fixtures.wbfs_file`-Bausteinen); NTFS/ext4 über `mkntfs`/`mkfs.ext4`, lokal per Docker, in CI nativ (`apt-get install ntfs-3g`). Images werden nie eingecheckt.
- Modify: `CMakeLists.txt` (`NL_FS_NTFS`, `NL_FS_EXT`), `.github/workflows/build.yml` (Matrix ON/ON, OFF/OFF), `tools/dolphin_smoke.py` (`--sd-disk <layout>`), `src/core/device_names.cpp` (Ntfs/Ext → `mount=true`, wenn der Schalter an ist), `src/platform/storage.cpp` (`ntfsMount`/`ext2Mount` nur lesend, `writable=false`), `THIRD_PARTY.md`, `LICENSES/`, `tests/host/core/device_names_test.cpp`

**Interfaces:**
- Consumes: `assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts)`, `DeviceTable::mountAll(const UsbMount&)` (M12 Task 2).
- Produces:
  - `struct FsSupport { bool ntfs; bool ext; bool wbfs; };` als zusätzlicher Parameter `assignDevices(..., FsSupport support)`. Der bisherige Aufruf übergibt `{false,false,false}` und bleibt gültig.
  - Namensvergabe: Jede eingehängte Partition, egal welches Dateisystem, bekommt den nächsten Namen aus `usb`, `usb2`…`usb8` in Tabellenreihenfolge. WBFS-Partitionen bekommen ebenfalls einen Namen (Task 4).
  - `Device::fs` (`FsKind`) für Preflight und Systeminfo.

- [ ] **Step 1: Failing tests** `device_names_test.cpp`:
  - FAT + NTFS mit `ntfs=true` → `usb` (fat), `usb2` (ntfs, `mount=true`);
  - dieselbe Tabelle mit `ntfs=false` → NTFS `mount=false`, Grund „NTFS wird nicht unterstützt“;
  - ext4 vor FAT → `usb` (ext), `usb2` (fat);
  - exFAT bleibt immer `mount=false` mit dem Text aus M12.
  - `test_vendor_fs.py`: falscher Hash → Abbruch mit Dateiname; `VENDORED.txt` enthält Commit und Lizenz.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren; Mount nur mit Nur-Lese-Flag aus dem Spike.
- [ ] **Step 4:** Host-Suite, pytest grün; Wii-Build ON/ON und OFF/OFF je 0 Warnungen; `check_licenses.py` grün.
- [ ] **Step 5: Smoke.** Dolphin emuliert keine USB-Platte. Deshalb dient die SD als Rohgerät:
  - `dolphin_smoke.py --sd-disk fat32,ntfs` legt mit `make_disk_image.py` ein SD-Image mit MBR, FAT32-Partition 1 und NTFS-Partition 2 an;
  - `[debug] sd_as_disk = 1` lässt `mountAll` die SD wie eine USB-Platte behandeln (Partitionstabelle lesen, Namen `usb`/`usb2`); `<data>` bleibt auf `usb`.
  - Erwartet: `NL_DEV usb fat mounted`, `NL_DEV usb2 ntfs mounted ro`.
- [ ] **Step 6: Commit** `feat(storage): NTFS und ext nur lesend einhängen`

### Task 3: Extents aus NTFS und ext

**Files:**
- Create: `src/platform/fs_extents.hpp/.cpp`, `src/core/extent_units.hpp/.cpp`, `tests/host/core/extent_units_test.cpp`
- Modify: `src/platform/wii_disc.cpp` (Extent-Quelle nach `Device::fs`), `src/core/launch_plan.*` (`LaunchFailure::FileNotContiguousReadable`), `src/ui/strings.cpp`

**Interfaces:**
- Consumes: `FilePart{u64 size; std::vector<Extent> extents}`, `Extent{u32 fileSector, lba, count}` (`core/frag_list`, `core/fat_chain`).
- Produces:
  ```cpp
  // core/extent_units: Läufe der Bibliothek (in deren Einheit) → Extents in 512-Byte-Sektoren, absolut auf dem Gerät.
  struct FsRun { u64 fileUnit; u64 devUnit; u64 units; bool hole; };
  enum class ExtentError : u8 { Unsupported, Overflow, Gap };
  Result<std::vector<Extent>, ExtentError> runsToExtents(std::span<const FsRun> runs, u32 unitBytes, u64 partLba,
                                                         u32 deviceSectorBytes, u64 fileSize);
  // platform/fs_extents: fragt die Bibliothek nach den Läufen einer Datei.
  Result<FilePart, ExtentError> fileExtentsFor(const Device& dev, std::string_view path);
  ```
  - `hole == true` (sparse, `uninit`) → `Unsupported`; komprimiert oder resident → `Unsupported` (Erkennung nach Spike Step 3).
  - Lücken in der Dateiabdeckung → `Gap`; LBA über `u32` → `Overflow`.
  - Benachbarte Läufe werden verschmolzen wie bei FAT.

- [ ] **Step 1: Failing tests** `extent_units_test.cpp`:
  - 4-KiB-Cluster, zwei Läufe → Extents in 512-Byte-Sektoren mit Partitions-LBA addiert;
  - 4-KiB-Gerätesektoren: `partLba` in Gerätesektoren wird ×8 umgerechnet;
  - Lauf mit `hole` → `Unsupported`;
  - Lücke zwischen Läufen → `Gap`;
  - letzte Einheit nur teilweise belegt → Extent endet bei `ceil(fileSize/512)`;
  - LBA ≥ 2³² Sektoren → `Overflow`;
  - zwei angrenzende Läufe → ein Extent.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren; `fileExtentsFor` nutzt die Funktionen aus Spike Step 3.
- [ ] **Step 4:** Host-Suite grün; Wii-Build 0 Warnungen.
- [ ] **Step 5: Smoke.** `--sd-disk fat32,ntfs` mit einem Wii-Test-ISO aus `make_fixtures.disc_header` (auf 64 MiB aufgefüllt) auf der NTFS-Partition; M9-Dry-Run → `NL_WII_FRAGS n=<k> verify=ok dev=usb2`. Dasselbe mit `fat32,ext4`.
- [ ] **Step 6: Commit** `feat: Fragmentliste aus NTFS- und ext-Extents`

### Task 4: Preflight je Dateisystem

**Files:**
- Modify: `src/core/launch_plan.*`, `src/core/emu_profiles.*` (Feld `fs`), `src/core/emulator_table.*` (`fs = fat|fat,ntfs|…` aus `emulators.ini`), `src/ui/strings.cpp`, `tests/host/core/launch_plan_test.cpp`, `tests/host/core/emu_profiles_test.cpp`

**Interfaces:**
- Consumes: `EmuProfile{id, folder, consoleMask, args, acceptsZip, fatOnly}` (M13 Task 3) – `fatOnly` wird durch `fsMask` ersetzt.
- Produces:
  - `EmuProfile::fsMask` (Bits aus `FsKind`, Standard nur `Fat`); `fatOnly` entfällt, Aufrufer migriert.
  - `LaunchFailure::NintendontNeedsFat` („Nintendont braucht FAT32“), `LaunchWarning::EmulatorFs` („<Emulator> liest dieses Dateisystem eventuell nicht“).
  - Regeln:
    - GC-Spiel auf Nicht-FAT → `NintendontNeedsFat` (Fehler, kein Start).
    - ROM auf Gerät, dessen `fs` nicht in `fsMask` liegt → `EmulatorFs` (Warnung, Start nach Bestätigung).
    - Wii-Spiel: jedes eingehängte Dateisystem erlaubt.

- [ ] **Step 1: Failing tests:**
  - GC auf `usb2` (ntfs) → `NintendontNeedsFat`;
  - SNES auf ext mit Standardprofil → `EmulatorFs`;
  - `emulators.ini` mit `fs = fat,ntfs` → keine Warnung auf NTFS;
  - Wii auf ext → kein Fehler.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Commit** `feat: Preflight je Dateisystem`

### Task 5: WBFS-Partitionen

**Files:**
- Create: `src/core/wbfs_part.hpp/.cpp`, `tests/host/core/wbfs_part_test.cpp`, `tests/host/support/wbfs_part_image.hpp` (erzeugt eine Partition mit n Slots aus `disc_images`)
- Modify:
  - `src/core/wbfs_map.*`: `parseWbfsHead(head, slot)`;
  - `src/core/scanner.*`: Gerät mit `fs == Wbfs` → Spiele aus den Slots, Pfad `wbfs:<slot>`;
  - `src/platform/wii_disc.cpp`: `wbfs:`-Pfad → ein Extent ab Partitions-LBA;
  - `src/core/device_names.cpp`: Wbfs → `mount=true` (ohne Dateisystem, nur Rohzugriff);
  - `src/platform/storage.cpp`: WBFS-Gerät ohne `fatMount`, `writable=false`;
  - Cover/Metadaten: Schlüssel = ID6 wie bei Dateien.

**Interfaces:**
- Consumes: `WbfsLayout`, `wbfsHeadBytes(u8)`, `parseWbfsHead(std::span<const u8>)`, `buildWbfsFragList(const WbfsLayout&, std::span<const FilePart>)`.
- Produces:
  ```cpp
  // core/wbfs_part: Partitionskopf (wbfs_head_t im ersten Sektor, Disc-Tabelle danach).
  struct WbfsPartHead { u32 nHdSec; u8 hdSecShift; u8 wbfsSecShift; std::vector<u8> discTable; };
  Result<WbfsPartHead, FragError> parseWbfsPartHead(std::span<const u8> sector0);
  std::vector<u32> usedSlots(const WbfsPartHead& h);               // discTable[i] != 0
  u64 slotInfoOffset(const WbfsPartHead& h, u32 slot);             // Byte-Offset der Disc-Info des Slots
  // core/wbfs_map: bisheriger Aufruf = Slot 0 einer Datei; Partition übergibt den Slot.
  Result<WbfsLayout, FragError> parseWbfsHead(std::span<const u8> head, u32 slot = 0);
  ```
  - Spielpfad `wbfs:<slot>` (dezimal). Der Scanner liest je Slot den Disc-Kopf (ID6, Titel) aus dem ersten belegten WBFS-Block.
  - Fragmentliste: `FilePart{ size = nHdSec × 512, extents = { {0, partLba, nHdSec} } }`, dann `buildWbfsFragList` unverändert.

- [ ] **Step 1: Failing tests** `wbfs_part_test.cpp`:
  - Partition mit drei Slots, Slot 1 leer → `usedSlots == {0, 2}`;
  - `parseWbfsHead(head, 2)` liefert die `wlba` von Slot 2;
  - Slot-Index ≥ Slotzahl → `WbfsInvalid`;
  - kaputter Disc-Kopf in Slot 0 → Scanner überspringt ihn, `skipped == 1`, Slot 2 erscheint;
  - Fragmentliste für Slot 2 mit `partLba = 2048` → erster Eintrag zeigt auf `2048 + wlba[0] × (wbfsSecSize/512)`;
  - bestehende `wbfs_map`-Tests (Datei, Slot 0) unverändert grün.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren.
- [ ] **Step 4:** Host-Suite grün; Wii-Build 0 Warnungen.
- [ ] **Step 5: Smoke.** `--sd-disk fat32,wbfs` (Task 2) mit zwei erzeugten Test-Discs in der WBFS-Partition. `[debug] list_games` → beide ID6 mit `dev=usb2 path=wbfs:0|wbfs:1`; M9-Dry-Run für Slot 1 → `NL_WII_FRAGS … verify=ok`.
- [ ] **Step 6: Commit** `feat: Wii-Spiele von WBFS-Partitionen`

### Task 6: Hardware-Abnahme M16 (optional nach Spec 13)

**Files:**
- Create: `docs/acceptance/m16-wii.md`

| Punkt | Prüfung |
|---|---|
| W1 | NTFS-Platte: Gerät erscheint als `usbN` (nur lesend) in der Systeminfo |
| W2 | Wii-Spiel (ISO und WBFS-Datei) von NTFS startet |
| W3 | GC-Spiel auf NTFS: Preflight „Nintendont braucht FAT32“ |
| W4 | ext4-Partition: Wii-Spiel startet |
| W5 | WBFS-Partition: alle Spiele erscheinen, eines startet |
| W6 | Gemischte Platte (FAT32 + NTFS): Namen und Cover stimmen nach Neustart |

- [ ] **Step 1:** Checkliste schreiben; nicht geprüfte Punkte stehen als „ungeprüft“ in den Release-Notes. **Step 2: Commit** `docs: Hardware-Abnahme M16`
