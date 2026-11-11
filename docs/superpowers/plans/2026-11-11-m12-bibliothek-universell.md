# M12 Bibliothek universell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Launcher findet seine Daten und Spiele auf jeder SD/USB-Kombination: Datenordner aus dem Startgerät, mehrere Partitionen (MBR, EBR, GPT), Quellenliste, frei wählbare Ordner und Endungen, mehr Wii- und GC-Dateinamensformen, ZIP-ROMs.

**Architecture:**
- Neue plattformfreie Bausteine, alle host-getestet:
  - `core/partition_table` liest die Partitionstabelle über das vorhandene `BlockDevice`;
  - `core/device_names` vergibt die Gerätenamen;
  - `core/data_dir` wählt den Datenordner;
  - `core/library_sources` liest `[library]`/`[paths]`/`[extensions]`;
  - `core/library_merge` führt die Indizes mehrerer Geräte zusammen.
- `platform/storage` hängt jede FAT-Partition über `fatMount` mit Startsektor ein.
- Der Scanner läuft je Gerät mit `ScanRules`. `Game` bekommt `u8 device`. Jeder Zugriff, der heute `libRoot` nutzt, fragt die Gerätetabelle.

**Tech Stack:** C++20, libfat (`fatMount`), doctest, Dolphin-Smoke.

**Spec:** `docs/superpowers/specs/2026-11-11-v2-universell-design.md` – 1, 2, 5, 13; Basis-Spec 5.1–5.4, 6.1.

**Basis-Branch:** `feat/m12-bibliothek` von `main` nach M11.

## Global Constraints

- Eine v1-`config.ini` startet unverändert:
  - `[general] library = usb|sd` wirkt wie `[library] sources = usb|sd`;
  - der Index `<gerät>:/newlauncher/cache/index.bin` behält sein Format (`kIndexVersion` bleibt 1).
- Gerätenamen: `sd`, `usb` (erste FAT-Partition), `usb2` … `usb8`. Mehr als 8 USB-Partitionen werden gelistet, aber nicht eingehängt.
- exFAT wird erkannt und mit dem Text „exFAT wird nicht unterstützt – bitte FAT32“ abgelehnt. NTFS/ext erscheinen bis M16 als „nicht unterstützt“.
- Ordner sind relativ zur Gerätewurzel und werden bis Tiefe 2 durchsucht. Die ID6 von Wii/GC kommt immer aus dem Disc-Kopf.
- `core/` ohne ogc; Wii-Build warnungsfrei mit `NL_TLS` ON und OFF.
- Alle Smokes aus M0–M11 bleiben grün (`bash $W/accept7.sh` plus M8–M11-Smokes).

## Review Focus

1. Kaputte Partitionstabellen:
   - EBR-Kette mit Schleife oder Verweis außerhalb der Platte;
   - GPT mit falscher Kopf-CRC;
   - Partition mit Länge 0;
   - Platte ohne Tabelle („Superfloppy“, FAT direkt in Sektor 0);
   - 4-KiB-Sektoren.
   - Erwartet: keine Endlosschleife, kein Lesen jenseits der Platte, Superfloppy als eine Partition.
2. `argv[0]` fehlt, ist leer oder zeigt auf ein Gerät, das nicht eingehängt ist (wiiload, Dolphin): Der Datenordner fällt auf `sd:` bzw. USB zurück. Kein Absturz, `NL_DATA` geloggt.
3. Ein Spiel liegt auf zwei Geräten und eines davon fehlt beim nächsten Start: Es erscheint vom verbliebenen Gerät. Favoriten und Verlauf (Schlüssel) bleiben erhalten.
4. `[paths]` mit leerem Eintrag, `..`, absolutem Pfad oder 50 Ordnern: Ungültige Teile werden verworfen und mit `NL_CFG_BAD paths.<kürzel>` geloggt. Es gibt höchstens 8 Ordner je Konsole.
5. Ein Wii-Spiel auf `usb2`: Die Fragmentliste nutzt die Start-LBA **dieser** Partition. Die Gegenprobe aus M9 schlägt bei falscher Partition fehl, statt ein falsches Spiel zu starten.

---

### Task 1: Partitionstabellen

**Files:**
- Create: `src/core/partition_table.hpp`, `src/core/partition_table.cpp`, `tests/host/core/partition_table_test.cpp`, `tests/host/support/disk_image.hpp` (Bauer für MBR/EBR/GPT-Abbilder im Speicher, `MemBlockDevice`)

**Interfaces:**
- Produces:
  ```cpp
  enum class FsKind : u8 { Fat, ExFat, Ntfs, Ext, Wbfs, Unknown };
  struct PartitionInfo { u64 lba; u64 sectors; u8 mbrType; FsKind kind; bool logical; };
  inline constexpr std::size_t kMaxPartitions = 16;
  FsKind detectFs(std::span<const u8> firstSector);   // Bootsektor bzw. Superblock-Sektor
  Result<std::vector<PartitionInfo>> readPartitions(BlockDevice& dev, u64 deviceSectors);
  ```
- Regeln:
  - **Sektor 0 ist ein FAT-, exFAT- oder NTFS-Bootsektor bzw. trägt die WBFS-Magic:** eine Partition ab LBA 0 (Superfloppy).
  - **MBR mit Typ `0xEE`:** GPT ab LBA 1.
    - Kopf-CRC32 und die CRC der Einträge werden geprüft (vorhandenes `core/crc32`).
    - Bis zu 128 Einträge, Typ-GUID ≠ 0.
  - **MBR-Typen `0x05`/`0x0F`:** EBR-Kette.
    - Höchstens 32 Glieder.
    - Jede EBR-LBA muss innerhalb der erweiterten Partition liegen.
    - Eine bereits besuchte LBA bricht ab.
  - **Bestimmung der Art:** über `detectFs` auf dem ersten Sektor der Partition, nicht über das Typbyte allein.
    - ext: Superblock bei Byte 1024, Magic `0xEF53` an Offset 56. Dafür wird der zweite 512-Byte-Block bzw. der richtige Teil eines 4-KiB-Sektors gelesen.
  - Partitionen mit Länge 0 oder jenseits von `deviceSectors` werden übersprungen.

- [ ] **Step 1: Failing tests:**

  | Test | Eingabe | Erwartet |
  |---|---|---|
  | `mbr_four_primary` | MBR mit vier primären Partitionen | FAT, NTFS, exFAT, ext |
  | `ebr_chain_three_logical` | EBR-Kette mit drei logischen Partitionen | `logical = true` |
  | `ebr_loop_stops` | EBR-Kette mit Schleife | zwei Partitionen, keine weiteren |
  | `gpt_two_entries` | GPT mit zwei Einträgen | beide Partitionen |
  | `gpt_bad_crc` | GPT mit falscher CRC | InvalidData |
  | `superfloppy` | Abbild ohne Partitionstabelle | eine Partition ab LBA 0 |
  | `wbfs_partition_detected` | Partition mit WBFS-Magic | `FsKind::Wbfs` |
  | `sector_4k` | Abbild mit 4-KiB-Sektoren | Erkennung funktioniert |
  | `zero_length_skipped` | Partition mit Länge 0 | übersprungen |
  | `beyond_disk_skipped` | Partition jenseits der Platte | übersprungen |
- [ ] **Step 2:** `& "$W\ht.ps1" "*partition*"` → FAIL. **Step 3:** implementieren. **Step 4:** PASS, ganze Host-Suite grün.
- [ ] **Step 5: Commit** `feat(core): Partitionstabellen MBR/EBR/GPT mit Dateisystemerkennung`

### Task 2: Gerätetabelle und Einhängen mehrerer Partitionen

**Files:**
- Create: `src/core/device_names.hpp`, `src/core/device_names.cpp`, `tests/host/core/device_names_test.cpp`
- Modify: `src/platform/storage.hpp`, `src/platform/storage.cpp`, `src/platform/wii_disc.cpp` (`RawDevice` + `findFatVolume` ab Partitions-LBA statt Suche ab Sektor 0), `src/app/main.cpp`

**Interfaces:**
- Produces (`core/device_names`):
  ```cpp
  struct DeviceSlot { std::string name; bool usb; u64 lba; FsKind kind; bool mount; std::string reason; };
  // SD (falls vorhanden) → "sd"; USB-Partitionen in Tabellenreihenfolge: erste Fat → "usb", weitere Fat → "usb2"…"usb8";
  // ExFat/Ntfs/Ext/Wbfs/Unknown → mount=false, reason gesetzt (M16 ändert Ntfs/Ext/Wbfs); >8 → mount=false, reason "zu viele Partitionen".
  std::vector<DeviceSlot> assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts);
  ```
- Produces (`platform/storage`):
  ```cpp
  struct Device { DeviceSlot slot; bool mounted; bool writable; u64 totalBytes, freeBytes; };
  class DeviceTable {
  public:
    void mountAll(const UsbMount& usb);         // SD + jede Fat-Partition über fatMount(name, io, lba, 8, 64)
    std::span<const Device> devices() const;
    const Device* byName(std::string_view n) const;
    std::string root(u8 index) const;           // "usb2:" …
    void unmountAll();
  };
  ```
- `computeWiiFrags` bekommt statt `libRoot` das `Device`. `RawDevice` liest relativ zur Platte, `findFatVolume` startet bei `slot.lba`.
- Die M9-Gegenprobe bleibt unverändert.

- [ ] **Step 1: Failing tests** `device_names_test.cpp`:
  - SD + zwei FAT-Partitionen → `sd`, `usb`, `usb2`;
  - exFAT zwischen zwei FAT-Partitionen → `usb`, (exFAT ohne Namen, `mount=false`), `usb2`;
  - zehn FAT-Partitionen → acht eingehängt, zwei mit Grund;
  - keine SD → kein `sd`-Eintrag.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren (core, dann platform).
- [ ] **Step 4:**
  - Host-Suite grün; Wii-Build 0 Warnungen.
  - Dolphin-Smoke: Log zeigt `NL_DEV sd fat mounted` (Dolphin hat nur die SD).
  - M9-Dry-Run-Smoke grün, also auch die Gegenprobe mit LBA.
- [ ] **Step 5: Commit** `feat(storage): Gerätetabelle, mehrere FAT-Partitionen je USB-Gerät`

### Task 3: Datenordner `<data>`

**Files:**
- Create: `src/core/data_dir.hpp`, `src/core/data_dir.cpp`, `tests/host/core/data_dir_test.cpp`
- Modify: `src/app/main.cpp` (`argv[0]` durchreichen, `NL_DATA <pfad>`), jede Stelle mit festem `sd:/newlauncher`.
  - Liste per `grep -rn "sd:/newlauncher" src` zu Beginn ins Ledger.
  - Betroffen sind Log, Config, `emulators.ini`, `games.ini`, Crash-Report, Musik, Favoriten.

**Interfaces:**
- Produces:
  ```cpp
  struct DataCandidate { std::string device; bool writable; bool hasDataDir; };
  struct DataDirChoice { std::string path; bool create; bool readOnly; };  // path z. B. "usb2:/newlauncher"
  std::string deviceFromArgv0(std::string_view argv0);   // "usb:/apps/x/boot.dol" → "usb"; sonst ""
  DataDirChoice chooseDataDir(std::string_view argv0, std::span<const DataCandidate> devs);
  ```
- Reihenfolge nach Spec 5.1:
  1. Startgerät mit Ordner;
  2. `sd` mit Ordner;
  3. erstes beschreibbares USB-Gerät mit Ordner;
  4. sonst anlegen auf dem Startgerät (beschreibbar), dann `sd`, dann erstem beschreibbarem USB;
  5. sonst `readOnly = true` auf dem ersten Gerät mit Ordner bzw. `path` leer.

- [ ] **Step 1: Failing tests:**
  - jede der vier Stufen;
  - `argv0` leer, `"/apps/x/boot.dol"` ohne Gerät und `"usb9:/…"` (nicht eingehängt) → Rückfall;
  - nichts beschreibbar → `readOnly`;
  - Groß-/Kleinschreibung `USB:` → `usb`.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren, alle Pfade auf `<data>` umstellen. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Smoke.** Dolphin startet ohne `argv`, Marker `NL_DATA sd:/newlauncher`. `accept7.sh` grün.
- [ ] **Step 6: Commit** `feat: Datenordner folgt dem Startgerät`

### Task 4: Quellen, Ordner und Endungen in der Konfiguration

**Files:**
- Create: `src/core/library_sources.hpp`, `src/core/library_sources.cpp`, `tests/host/core/library_sources_test.cpp`
- Modify: `src/core/config.hpp/.cpp` (+ `config_test.cpp`), `src/core/console.hpp/.cpp` (`folder` → `std::array<const char*, 2> folders`, Endungen auf `std::array<const char*, 5>`), `src/core/settings_model.*` (Zeile „Bibliothek“ zeigt die Quellenliste)

**Interfaces:**
- Produces:
  ```cpp
  struct SourceSpec { bool autoMode = true; std::vector<std::string> devices; };      // [library] sources
  struct ConsolePaths { std::vector<std::string> folders; std::vector<std::string> extensions; };
  struct LibraryRules { SourceSpec sources; std::array<ConsolePaths, kConsoleCount> paths; };
  LibraryRules libraryRulesFromIni(const IniDoc& ini);   // inkl. Alt-Schlüssel [general] library
  std::string sourcesToText(const SourceSpec& s);         // "auto" bzw. "usb,usb2,sd"
  std::vector<std::string> orderedSources(const SourceSpec& s, std::span<const std::string> mounted);  // auto = alle in Gerätereihenfolge
  ```
- Standardordner und -endungen genau nach Spec 5.3:
  - Wii `wbfs|wii`, `.wbfs .iso`;
  - GC `games|gc`, `.iso .gcm .ciso`; `.nkit.iso` zählt als `.iso`; FST-Ordner gesondert in Task 5;
  - N64 `roms/n64`;
  - übrige Konsolen `roms/<kürzel>` mit den Endungen aus Basis-Spec 5.3.
- `Config` bekommt `LibraryRules library`. Das Feld `LibraryDevice` bleibt nur als Alt-Leser.

- [ ] **Step 1: Failing tests:**
  - `sources = usb2, sd` → Reihenfolge erhalten, Leerzeichen getrimmt;
  - `sources = auto`;
  - fehlt `[library]`, aber `[general] library = sd` → `{sd}`;
  - `[paths] wii = games/wii|wbfs` → zwei Ordner;
  - `[paths] nes = ../x|/abs||ok` → nur `ok`, plus Log `NL_CFG_BAD paths.nes`;
  - 50 Ordner → 8;
  - `[extensions] gba = .gba|.zip` → genau diese;
  - `orderedSources(auto, {sd, usb})` → Gerätereihenfolge;
  - `orderedSources({usb3}, {sd, usb})` → leer.
  - `configToIniText` bewahrt Kommentare und schreibt `[library]`.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün (v1-Config-Test aus M0 unverändert grün).
- [ ] **Step 5: Commit** `feat(config): Quellenliste, Ordner und Endungen je Konsole`

### Task 5: Scanner mit Regeln – Namensformen, FST-Ordner, ZIP

**Files:**
- Modify: `src/core/scanner.hpp/.cpp`, `tests/host/core/scanner_test.cpp`, `src/core/launch_plan.cpp` (Preflight-Gründe), `src/core/nin_cfg.cpp` (Pfad für FST-Ordner), `tests/host/support/disc_images.hpp`, `tests/host/support/zip_builder.hpp`

**Interfaces:**
- Produces:
  ```cpp
  struct ScanRules { std::array<ConsolePaths, kConsoleCount> paths; u8 zipConsoles = 0b0101'0111; /* Bits nach Console: Nes 0, Snes 1, Gb 2, Gbc 4, Gba 6 */ u8 maxDepth = 2; };
  Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view root, const ScanRules& rules, const IndexData* previous,
                                 const MetaDb* meta, ScanProgress progress = nullptr, void* user = nullptr);
  ```
  `zipConsoles` ist eine Bitmaske über `Console`-Werte. M13 schränkt sie über die Emulator-Profile ein.
- **Wii-Namensformen** (Spec 5.3): `Titel [ID6]/ID6.wbfs`, `ID6.wbfs`, `ID6_Titel.wbfs`, `Titel [ID6].wbfs`, `Titel.wbfs`, jeweils auch `.iso`, mit `.wbf1`-Teilen.
  - Der Ersatztitel aus dem Dateinamen gilt nur, wenn Disc-Kopf und `meta.bin` keinen liefern.
- **GC:**
  - FST-Ordner: ein Ordner mit `sys/boot.bin`. ID6 aus `boot.bin` (gleiches Kopflayout wie das ISO), Pfad = Ordner.
  - Die Endungen `.gcm` und `.ciso` lesen den Kopf wie ISO; CISO hat den Disc-Kopf bei `0x8000`.
- **ZIP:** Eintrag, wenn die Konsole in `zipConsoles` liegt und `core/zip_reader` mindestens einen Eintrag mit einer Endung der Konsole findet. Schlüssel aus dem ZIP-Namen.
- **Step 1 (vor dem Test): Nintendont-Formate prüfen.**
  - Im Nintendont-Quelltext nachsehen, welche Formate der Loader annimmt: ISO, GCM, CISO, FST, NKit. Quellen: `loader/source/main.c`, `kernel/ISO.c`, Commit im Ledger.
  - Nicht startbare Formen bekommen in `preflightGame` den neuen Fehler `LaunchFailure::NinFormat` mit Kontext Endung.
  - Der Text lautet „Nintendont startet dieses Format nicht: <endung>“ (DE/EN).

- [ ] **Step 2: Failing tests** im `scanner_test.cpp` mit `MemFs`:

  | Test | Erwartet |
  |---|---|
  | jede Wii-Namensform | richtige ID6, richtiger Titelvorrang |
  | `Titel [RMGP01]/RMGP01.wbfs` + `.wbf1` | ein Eintrag, Flag `Split` |
  | GC FST-Ordner `Zelda [GZLE01]/sys/boot.bin` | Eintrag mit Ordnerpfad |
  | `.ciso`-Kopf | ID6 bei `0x8000` gelesen |
  | Ordner `games/gc/sub/sub2/x.iso` (Tiefe 3) | nicht gefunden |
  | Tiefe 2 | gefunden |
  | ZIP mit `.nes`-Eintrag | gefunden |
  | ZIP nur mit `.txt` | übersprungen |
  | ZIP bei `zipConsoles = 0` | übersprungen |
  | zwei Ordner für eine Konsole | beide gescannt, gleicher Schlüssel nur einmal |

  - `launch_plan_test`: GC-Format laut Step 1 nicht startbar → `NinFormat`.
- [ ] **Step 3:** FAIL. **Step 4:** implementieren. **Step 5:** Host-Suite grün.
- [ ] **Step 6: Commit** `feat(scan): Ordnerregeln, Wii/GC-Namensformen, FST-Ordner, ZIP-ROMs`

### Task 6: Mehrere Geräte zusammenführen

**Files:**
- Create: `src/core/library_merge.hpp`, `src/core/library_merge.cpp`, `tests/host/core/library_merge_test.cpp`
- Modify:
  - `src/core/game.hpp` (`u8 device = 0`);
  - `src/core/library_store.*` (`setGames(MergedLibrary, const MetaDb*)`);
  - `src/platform/scan_worker.*` (Geräte nacheinander, Index je Gerät; nicht beschreibbar → `<data>/cache/<gerät>/index.bin`);
  - `src/core/cover_paths.*` (Wurzel je Spiel: `coverRoot(device, writable, dataDir)`);
  - `src/app/main.cpp`, `src/app/cover_service.*` (alle `libRoot`-Stellen über `DeviceTable::root(game.device)`);
  - `src/ui/details_scene.cpp`, `src/ui/strings.cpp` (Zeile „Gerät: USB 1 (auch auf: SD)“).

**Interfaces:**
- Produces:
  ```cpp
  struct DeviceIndex { std::string device; IndexData index; };
  struct MergedGame { IndexGame game; u8 device; std::vector<u8> alsoOn; };
  struct MergedLibrary { std::vector<std::string> devices; std::vector<MergedGame> games; };
  MergedLibrary mergeIndexes(std::span<const DeviceIndex> inOrder);  // gleicher Schlüssel: erstes Gerät gewinnt
  std::string deviceLabel(std::string_view device);                   // "sd" → "SD", "usb" → "USB 1", "usb2" → "USB 2"
  ```

- [ ] **Step 1: Failing tests:**
  - zwei Geräte mit überlappenden Schlüsseln → Anzahl, Gewinner, `alsoOn`;
  - Reihenfolge `{usb, sd}` gegen `{sd, usb}` → anderer Gewinner;
  - leeres Gerät;
  - `deviceLabel` für `sd`, `usb`, `usb2`, `usb8`;
  - `cover_paths`: beschreibbar → `usb2:/newlauncher/covers/…`; nicht beschreibbar → `<data>/cache/usb2/covers/…`;
  - `library_store`: Favorit bleibt nach einem Gerätewechsel erhalten.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün; Wii-Build 0 Warnungen.
- [ ] **Step 5: Smokes.**
  - Neuer Dolphin-Smoke `--sd-layout multi`. Die SD-Fixture enthält Spiele in `roms/nes` und in eigenem `[paths] nes = emu/nes`, dazu ein ZIP-ROM. Erwartet `NL_SCAN games=<n>` mit der erwarteten Zahl.
  - Debug-Zeile `[debug] list_games = 1` loggt `NL_GAME <key> <device>`. Prüfung auf Gerät `sd` und ZIP-Eintrag.
  - `accept7.sh` grün.
- [ ] **Step 6: Commit** `feat: Bibliothek über mehrere Geräte`

### Task 7: Hardware-Abnahme M12

**Files:**
- Create: `docs/acceptance/m12-wii.md`

| Punkt | Prüfung |
|---|---|
| W1 | v1-`config.ini` unverändert: Bibliothek, Favoriten, Cover wie vorher |
| W2 | SD entnommen: `NL_DATA usb:/newlauncher`, Launcher voll bedienbar |
| W3 | App von USB gestartet (`usb:/apps/newlauncher`), Daten liegen auf USB |
| W4 | zweite FAT-Partition (falls die Platte das zulässt): `usb2` in der Liste, Spiele gefunden, Wii-Spiel von `usb2` startet |
| W5 | dasselbe Spiel auf SD und USB: einmal sichtbar, Detailansicht „auch auf“ |
| W6 | ZIP-ROM startet in FCEUGX/Snes9xGX |
| W7 | exFAT-Stick: Hinweis in der Liste, kein Absturz |

- [ ] **Step 1:** Checkliste schreiben. **Step 2: Commit** `docs: Hardware-Abnahme M12`
