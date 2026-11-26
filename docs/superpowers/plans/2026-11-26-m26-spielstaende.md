# M26 Lokale Spielstände – Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Ziele 1–4 der Spec: Spielstand-Einheiten finden, als ZIP sichern, sicher (mit Journal) wiederherstellen,
auch in den NAND-Save-Ordner, und das in einer Save-Manager-Szene anbieten.

**Architecture:** Formate (Deflate, ZIP schreiben/lesen im Speicher, Paket mit Meta und Inhalts-Hash), Locator,
Startziel → Abfrage, Sicherungen und Journal liegen host-testbar in `src/core` und arbeiten gegen `FileSystem`.
`src/platform` liefert ISFS-Patch, ein `FileSystem` für `nand:/`, ein Routing-Dateisystem und einen Worker-Thread.
Die UI bekommt einen Auswahldialog und die Szene „Spielstand“.

**Tech Stack:** C++20, devkitPPC/libogc (ISFS), doctest, Dolphin-Smokes.

**Spec:** `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md` (Ziele 1–4)

## Global Constraints
- UI-Texte DE und EN; `src/core` ohne `<ogc/…>`; kein throw/RTTI; 0 Warnungen mit und ohne TLS.
- NAND-Schreiben **nur** unter `title/<hi>/<lo>/data/` (echter NAND und EmuNAND); jeder andere Zielpfad → Fehler.
- Höchstens **16 MiB** roh je Einheit (`kMaxSaveUnitBytes = 16u * 1024 * 1024`), höchstens 5 Sicherungen je Einheit.
- Meta-Datei `newlauncher-save.ini` ist immer der erste ZIP-Eintrag; `format = 1`.
- Inhalts-Hash: SHA-256 über die nach Pfad (Byte-Vergleich) sortierten Zeilen `<pfad>\n<größe>\n<sha256 hex>\n`.
- Savestates (`.frz .fcs .sgm .st0–.st9 .state*`) und `.png` gehören nie zu einer Einheit.
- Löschungen werden nie übertragen: Wiederherstellen entfernt keine Datei, die vorher da war.
- Echte Wii: vor dem ersten NAND-Schreiben auf Hardware den Nutzer fragen, BootMii-NAND-Backup empfehlen.

## Review Focus
1. Stromausfall mitten im Wiederherstellen: Beim nächsten Start stellt das Journal den alten Stand her, auch neu
   hinzugekommene Dateien verschwinden wieder (`added` im Journal).
2. Ein Paket mit `../`, absolutem Pfad oder Pfad außerhalb der Dateiliste einer FAT-Einheit (z. B. `boot.dol`
   für eine GB-Einheit) wird abgelehnt, bevor irgendetwas geschrieben wird.
3. Leerer Save-Ordner (Spiel nie gespielt): Sichern meldet „nichts zu sichern“, Wiederherstellen legt den Ordner an.
4. Paket einer anderen Einheit (Meta `unit` ≠ Ziel) oder mit falschem Hash wird abgelehnt.
5. NAND gesperrt (`NotFound`/`NoAhbprot`): NAND-Einheiten zeigen „NAND gesperrt“, FAT-Einheiten gehen weiter.

---

### Task 1: Deflate-Kompressor
**Files:** `src/core/deflate.{hpp,cpp}`, `tests/host/core/deflate_test.cpp`, `CMakeLists.txt`/Testliste wie bei `inflate`
- Produces: `std::vector<u8> deflateRaw(std::span<const u8> in)` – roher Deflate-Strom (RFC 1951), feste
  Huffman-Codes (BTYPE 01), LZ77 mit Hash-Ketten (Fenster 32 KiB, Länge 3–258, Kettentiefe 32); ist die Ausgabe
  größer als gespeichert, gespeicherte Blöcke (BTYPE 00, ≤ 65535 Bytes je Block).
- [ ] Tests (FAIL): `deflateRaw: Rundlauf leer`, `… 1 Byte`, `… 1 MiB Nullen (Ausgabe < 4 KiB)`,
      `… 200 KiB Zufall (Ausgabe ≤ Eingabe + 5 je 64 KiB)`, `… Text mit Wiederholungen über 32 KiB Abstand`;
      jeweils `inflateRaw(deflateRaw(x), x.size()) == x`.
- [ ] Umsetzen, Tests PASS, Commit `feat: Deflate-Kompressor für Spielstand-Pakete`.

### Task 2: ZIP schreiben und aus dem Speicher lesen
**Files:** `src/core/zip_writer.{hpp,cpp}`, `src/core/zip_reader.{hpp,cpp}`, `tests/host/core/zip_writer_test.cpp`,
`tools/tests/test_zip_compat.py` (optional, siehe unten)
- Consumes: `deflateRaw`, `crc32`, `isSafeZipPath`, `inflateRaw`.
- Produces: `class ZipWriter { Result<void> add(std::string_view name, std::span<const u8> data, bool deflate, u32 dosTime); std::vector<u8> finish(); }`
  (unsicherer Name, doppelter Name oder > `kMaxZipEntries` → `InvalidData`; deflate nur, wenn kleiner);
  `u32 dosTimeFromUnix(u64 unix)`; in `zip_reader`: `Result<std::vector<ZipEntry>> readZipDirectory(std::span<const u8> zip)` und
  `Result<std::vector<u8>> extractZipEntry(std::span<const u8> zip, const ZipEntry& e)` mit denselben Prüfungen
  wie die `FileSystem`-Varianten (gemeinsamer interner Kern, kein Kopieren der Logik).
- [ ] Tests (FAIL): Rundlauf 3 Dateien (eine gespeichert, eine Deflate, ein Unterordner `a/b.bin`) über die
      Speicher-Variante; Reihenfolge bleibt; `add("../x")`, `add("a//b")`, doppelter Name → Fehler;
      `dosTimeFromUnix(1764115200)` = Datum 2025-11-26 00:00:00.
- [ ] Umsetzen, bestehende `zip_reader`-Tests bleiben grün.
- [ ] Kompatibilität: Host-Test schreibt `build/host/zip_writer_sample.zip`; `tools/tests/test_zip_compat.py`
      öffnet es mit `zipfile` und prüft `testzip() is None`, wenn die Datei existiert (sonst `skipTest`).
- [ ] Commit `feat: ZIP-Writer und ZIP-Lesen aus dem Speicher`.

### Task 3: Spielstand-Paket
**Files:** `src/core/save_archive.{hpp,cpp}`, `tests/host/core/save_archive_test.cpp`
- Produces:
  - `inline constexpr std::size_t kMaxSaveUnitBytes = 16u * 1024 * 1024;`
  - `struct SaveFile { std::string path; std::vector<u8> data; };`
  - `struct SaveMeta { u32 format = 1; std::string unit, title, device; u64 saved = 0; std::string hash; u32 files = 0; };`
  - `std::string contentHash(std::span<const SaveFile> files);` (hex, 64 Zeichen)
  - `Result<std::vector<u8>> packSave(SaveMeta meta, std::span<const SaveFile> files);` – setzt `hash`, `files`,
    schreibt Meta als INI (`[save]`), dosTime aus `meta.saved`; Summe > `kMaxSaveUnitBytes` → `InvalidData`.
  - `struct SavePackage { SaveMeta meta; std::vector<SaveFile> files; };`
  - `Result<SavePackage> unpackSave(std::span<const u8> zip, std::size_t maxRaw = kMaxSaveUnitBytes);` –
    erster Eintrag muss die Meta sein, `format == 1`, Summe der Größen ≤ maxRaw **vor** dem Entpacken, Hash
    muss passen, Ordner-Einträge werden übersprungen.
  - `Result<SaveMeta> readSaveMeta(std::span<const u8> zip);` (nur Meta, für die Liste der Sicherungen)
- [ ] Tests (FAIL): `contentHash unabhängig von der Reihenfolge`; `contentHash ändert sich bei 1 Byte`;
      `contentHash leer` = SHA-256 von "" (`e3b0c442…b855`); Rundlauf pack/unpack mit Meta-Feldern (Titel mit
      Umlaut); manipulierte Datei → `InvalidData`; fehlende Meta → `InvalidData`; `format = 2` → `VersionMismatch`;
      Größe 16 MiB + 1 → Fehler bei pack und bei unpack (Verzeichnisgröße, ohne zu entpacken).
- [ ] Umsetzen, PASS, Commit `feat: Spielstand-Paket mit Meta und Inhalts-Hash`.

### Task 4: Einheiten und Locator
**Files:** `src/core/save_locator.{hpp,cpp}`, `src/core/emulators.{hpp,cpp}`, `tests/host/core/save_locator_test.cpp`,
`tests/host/core/emulators_test.cpp`, Spec-Zeile zu `emulators.ini`
- Produces:
  - `enum class SaveStore : u8 { RealNand, EmuNand, Fat };`
  - `enum class SaveKind : u8 { WiiDisc, Channel, GcSingle, GcMulti, Rom };`
  - `struct SaveQuery { SaveKind kind; std::string id4; u64 titleId = 0; std::string root; bool jp = false; Console console = Console::Nes; std::string profile; std::string stem; std::string gameKey; std::string saveDir; std::vector<std::string> saveExt; };`
    `root`: `"nand:"` (echter NAND), `"<dev>:<ordner>"` (EmuNAND, z. B. `usb:/emunand`) oder `"<dev>:"` (Spielgerät GC).
  - `struct SaveUnit { std::string id; SaveStore store; std::string dir; std::vector<std::string> names; };`
    `dir` absolut ohne Ende-`/`; `names` leer = ganzer Ordner rekursiv (Wii/Kanal), sonst erlaubte Dateinamen.
  - `std::string saveUnitId(const SaveQuery& q);` – `wii-<ID4>`, `ch-<tid16 klein>`, `gc-<ID4>`, `gc-ninmem`,
    `gc-ninmemj`, ROM `<consoleInfo.key>-<slug>-<crc8>` (slug nach Spec Ziel 1, `crc8` = `%08x` von `crc32(gameKey)`).
  - `std::optional<SaveUnit> locateSave(FileSystem& fs, const SaveQuery& q);` – nullopt nur, wenn es keinen
    **möglichen** Ort gibt (ROM-Profil ohne bekannten Ordner); ein noch fehlender Ordner ist ein gültiger Ort.
  - `bool isSaveStateName(std::string_view name);`
  - `Result<std::vector<SaveFile>> readUnit(FileSystem& fs, const SaveUnit& u, std::size_t maxRaw = kMaxSaveUnitBytes);`
    (fehlender Ordner → leere Liste; Pfade relativ zu `dir`)
  - `bool unitAcceptsPath(const SaveUnit& u, std::string_view relPath);` – `names` leer: `isSafeZipPath`; sonst
    genau einer der Namen.
  - `EmulatorEntry` bekommt `std::string saveDir; std::vector<std::string> saveExt;` aus `save_dir`/`save_ext`
    (Komma-Liste, klein, mit Punkt) des Konsolen-Abschnitts.
- Regeln: Wii `<root>/title/00010000/<hex8(ID4)>/data`, existiert er nicht und `00010004/…` schon → der;
  Kanal `<root>/title/<hi8>/<lo8>/data`; GcSingle `<root>/saves`, names `{<ID4>.raw}`; GcMulti names
  `{ninmem.raw}` bzw. `{ninmemj.raw}`; ROM: Profil `fceugx` `{stem.sav}`, `snes9xgx` `{stem.srm, stem.rtc}`,
  `vbagx` `{stem.sav}`, Ordner `<dev>:/<app>/saves` (erst `sd:`, dann `usb:` mit vorhandenem Ordner, sonst
  `sd:`), `saveFolder` aus `sd:/apps/<app>/settings.xml`, dann `sd:/<app>/settings.xml` ersetzt den Ordner;
  `saveDir` gesetzt → Ordner und `saveExt` (Standard `.sav,.srm`) gewinnen über das Profil; sonst
  (not64, wii64, retroarch-*) nullopt.
- [ ] Tests (FAIL) mit `MemFileSystem`: Wii 00010000; Wii nur 00010004; Wii keiner → 00010000-Pfad; Kanal echter
      NAND und EmuNAND; GC single/multi/multi-j; fceugx Standard; snes9xgx mit `.rtc`; vbagx auf `usb:` mit
      vorhandenem Ordner; `saveFolder`-Override; `save_dir` in `[n64_not64]`; retroarch ohne Eintrag → nullopt;
      `saveUnitId` aller Arten (slug „Zelda: A Link to the Past (USA).sfc“ → `zelda-a-link-to-the-past-usa`, 40 Zeichen
      gekappt); `readUnit` lässt `.frz`/`.png` weg und liest Unterordner der Wii-Einheit; `unitAcceptsPath`
      lehnt `boot.dol` für eine GB-Einheit ab; `emulators.ini` mit `save_dir`/`save_ext` wird gelesen.
- [ ] Spec Ziel 1: `emulators.ini [<profil>]` → `[<konsolen-abschnitt>]` (`nes`, `snes`, `gb`, `gbc`, `gba`, `n64_not64`, `n64_wii64`).
- [ ] Umsetzen, PASS, Commit `feat: Spielstand-Einheiten finden`.

### Task 5: Startziel → Abfrage
**Files:** `src/core/save_query.{hpp,cpp}`, `tests/host/core/save_query_test.cpp`
- Consumes: `Game`, `LaunchTarget`, `EmulatorTable` (aufgelöst), `SaveQuery`.
- Produces: `std::optional<SaveQuery> saveQueryFor(const Game& g, const LaunchTarget& t, std::string_view libRoot, const EmulatorTable& emus);`
  - `Kind::Wii` ohne `channelPlanned`: WiiDisc, ID4 = erste 4 Zeichen von `g.key`, root = EmuNAND
    (`"<device>:<folder>"`) wenn `t.wii->emuNand` gesetzt und `!emuNandFull`, sonst `"nand:"`.
  - `Kind::Wii` mit `channelPlanned`: Channel, `titleId = t.wii->channelTitleId`, root = EmuNAND.
  - `Kind::Channel`: Channel, `titleId = t.titleId`, root `"nand:"`.
  - GC (`t.nintendont` gesetzt): `t.gc->memcardReal` → nullopt; `kNinCfgMcMulti` in `t.gc->nin.extraFlags` → GcMulti
    (`jp` bei Regionsbuchstabe `J` oder `K` an Stelle 4 der ID); sonst GcSingle; root = `libRoot`.
  - ROM (`Kind::Dol`, Konsole Nes…Gba, N64 über `t.n64`): profile/saveDir/saveExt aus dem Eintrag, stem =
    Dateiname von `g.path` ohne Endung, gameKey = `g.key`. Disc-Start ohne Gerät (`kNoDevice`) bei GC → nullopt.
- [ ] Tests (FAIL) auf Fixtures wie in `launch_plan_test.cpp`: je Zweig ein Fall, dazu `memcardReal` → nullopt,
      Wii mit `save=emu` → EmuNAND-Root, EmuNAND-Kanal mit `emuNandFull`.
- [ ] Umsetzen, PASS, Commit `feat: Spielstand-Abfrage aus dem Startziel`.

### Task 6: Sicherungen, Journal, Wiederherstellen
**Files:** `src/core/save_backup.{hpp,cpp}`, `tests/host/core/save_backup_test.cpp`, `tests/host/support/failing_fs.hpp`
- Consumes: Tasks 3 und 4.
- Produces:
  - `std::string backupDir(std::string_view dataDir, std::string_view unitId);` → `<data>/saves/<unit>`
  - `std::string backupFileName(u64 unix);` → `yyyymmdd-hhmmss.zip` (UTC)
  - `std::vector<std::string> listBackups(FileSystem& fs, std::string_view dataDir, std::string_view unitId);` neueste zuerst
  - `Result<void> pruneBackups(FileSystem&, std::string_view dataDir, std::string_view unitId, std::size_t keep = 5);`
  - `struct BackupInfo { std::string zipPath; std::string hash; bool empty = false; };`
  - `Result<BackupInfo> backupUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, std::string_view title, std::string_view device, u64 now);`
    – leere Einheit → `empty = true`, kein ZIP; danach `pruneBackups`.
  - `enum class RestorePhase : u8 { Writing, Verifying };`
  - `struct RestoreJournal { std::string unit, dir, backup; std::vector<std::string> added; RestorePhase phase; };` als INI
    `<data>/saves/restore.ini` (`std::string serializeJournal(const RestoreJournal&)`, `std::optional<RestoreJournal> parseJournal(std::string_view)`).
  - `Result<void> restoreUnit(FileSystem& fs, std::string_view dataDir, const SaveUnit& u, const SavePackage& pkg, std::string_view title, std::string_view device, u64 now);`
    Ablauf nach Spec Ziel 3: Meta-`unit` ≠ `u.id` → `InvalidData`; jeder Pfad `unitAcceptsPath` **vor** dem
    ersten Schreiben; `backupUnit`; Journal (Writing, `added` = Pfade ohne heutige Datei); je Datei
    `makeDirs` + `writeAtomic`; Phase Verifying; `readUnit` und Hash über die Paketpfade vergleichen;
    Journal löschen. Fehler nach dem Journal → sofort `rollback`, Fehler zurückgeben.
  - `enum class RecoverResult : u8 { None, RolledBack, Failed };`
  - `RecoverResult recoverRestore(FileSystem& fs, std::string_view dataDir);` – Sicherung zurückspielen (leer = nur
    `added` löschen), `added` löschen, Journal löschen.
- [ ] `FailingFs` (Wrapper um `MemFileSystem`, `failWriteAfter = n` lässt den n+1-ten `writeAtomic` scheitern).
- [ ] Tests (FAIL): Backup-Namen sortiert, 6. Sicherung löscht die älteste; leere Einheit → `empty`; Rundlauf
      backup → ändern → restore → gleicher Hash; Fehler nach Datei 2 von 3 → Ursprungszustand, `added` weg,
      kein Journal; Journal liegt (Absturz simuliert durch Abbruch nach Schreiben) → `recoverRestore` =
      `RolledBack`; kein Journal → `None`; falsche Einheit und unsicherer Pfad → nichts geschrieben
      (`fs.writes` unverändert); Datei, die nur lokal existiert, bleibt nach dem Restore erhalten.
- [ ] Umsetzen, PASS, Commit `feat: Sicherungen und Wiederherstellen mit Journal`.

### Task 7: NAND-Zugriff (Plattform)
**Files:** `src/platform/ios_patch.{hpp,cpp}`, `src/platform/isfs_fs.{hpp,cpp}`, `src/platform/routing_fs.{hpp,cpp}`
- Produces:
  - `enum class IosPatchResult : u8 { Applied, Already, NotFound, NoAhbprot, Emulated };`
    `IosPatchResult applyIsfsPermissionPatch();` – Dolphin (Gerät `/dev/dolphin` öffnet) → `Emulated`; ohne
    AHBPROT (`read32(0x0D800064) != 0xFFFFFFFF`) → `NoAhbprot`; sonst MEM2-Schutz aus (`0x0D8B420A` = 0),
    Rechteprüfung des FS-Moduls im IOS-Speicher suchen und patchen, Cache spülen; Muster nicht gefunden →
    `NotFound`. Das Muster ist bis Spike S1 unbestätigt: eine Konstante in der `.cpp` mit Kommentar „S1 offen“.
    `const char* iosPatchName(IosPatchResult)` für den Marker `NL_IOSPATCH <name>`.
  - `class IsfsFileSystem final : public FileSystem` für Pfade `nand:/…`: `list`, `read`, `readAll`,
    `writeAtomic` (Ziel muss unter `/title/*/*/data/` liegen, sonst `Unsupported`; schreibt
    `/tmp/nl_<crc32(pfad)>`, übernimmt per `ISFS_GetAttr` Eigentümer/Gruppe/Rechte der alten Datei bzw. des
    Ordners, `ISFS_Rename`), `makeDirs` (nur unterhalb von `data/`), `removeFile` (nur unterhalb von `data/`),
    `renameFile` → `Unsupported`.
  - `class RoutingFileSystem final : public FileSystem` – `nand:` → `IsfsFileSystem`, alles andere → das bisherige
    stdio-Dateisystem.
- [ ] Wii-Build mit und ohne TLS, 0 Warnungen. Commit `feat: NAND-Zugriff für Spielstand-Ordner`.

### Task 8: Save-Worker
**Files:** `src/platform/save_worker.{hpp,cpp}`
- Consumes: Task 6, Task 7.
- Produces: `class SaveWorker` (eigener LWP-Thread wie `cover_clear_worker`, ein Auftrag zugleich):
  `bool startBackup(SaveUnit u, std::string title)`, `bool startRestore(SaveUnit u, std::string zipPath, std::string title)`,
  `bool running() const`, `std::optional<SaveJobResult> poll()` mit
  `struct SaveJobResult { enum class Kind : u8 { Backup, Restore } kind; bool ok; std::string detail; BackupInfo backup; };`
  Der Haupt-Thread loggt die Marker. Gerätename = `config [general] device_name`, sonst `"Wii"`.
- [ ] Build 0 Warnungen, Commit `feat: Spielstand-Aufträge im eigenen Thread`.

### Task 9: Auswahldialog
**Files:** `src/ui/choice_dialog.{hpp,cpp}`, `tests/host/ui/choice_dialog_test.cpp`
- Produces: `class ChoiceDialog final : public Scene` mit `void open(const char* title, const char* text, std::span<const char* const> buttons, AppCommand onPick, int defaultIndex = 0);`
  (2–3 Knöpfe), `int picked() const` (−1 = abgebrochen); Links/Rechts wechselt, A wählt und sendet `onPick`, B bricht
  ab (−1, sendet `onPick` ebenfalls), Klick auf einen Knopf wählt ihn. Texte in festen Puffern wie `DialogScene`.
- [ ] Tests (FAIL): Rechts + A → 1 und Kommando; B → −1; Klick auf Knopf 2 → 2; Rechts am Ende bleibt; 3 Knöpfe passen in 640 px.
- [ ] Umsetzen, PASS, Commit `feat: Auswahldialog mit bis zu drei Knöpfen`.

### Task 10: Szene „Spielstand“ und Details-Zeile
**Files:** `src/ui/saves_scene.{hpp,cpp}`, `src/ui/details_scene.{hpp,cpp}`, `src/ui/scene.{hpp,cpp}` (AppCommand),
`src/ui/strings.hpp`, `tests/host/ui/saves_scene_test.cpp`, `tests/host/ui/details_scene_test.cpp`
- Produces:
  - `AppCommand::OpenSaves` (Details, game gesetzt), `BackupSave`, `RestoreSave` (Szene: `selectedBackup()`),
    `DeleteBackup`, `ConfirmRestore` (aus dem `ChoiceDialog`).
  - `struct SavesView { std::string unitId, location; u64 bytes = 0; u32 files = 0; enum class State : u8 { Ok, Empty, NandLocked, Unknown } state; std::vector<std::string> backups; bool busy = false; };`
    `void SavesScene::open(const Game& g, SavesView v)`, `void setView(SavesView v)`, `int selectedBackup() const`.
  - Knöpfe Sichern / Wiederherstellen (nur mit Sicherung) / Sicherung löschen; im Zustand `NandLocked` und
    `Unknown` nur die Liste. Bei `busy` sind die Knöpfe gesperrt.
  - Details: Zeile „Spielstand …“ unter „Cheats“ für jedes Spiel mit `hasSave` (`void setHasSave(bool)`), Fokus
    wie die Cheats-Zeile.
  - Strings (DE/EN): `DetailsSave` „Spielstand …“/„Save data …“, `SavesTitle` „Spielstand – %s“/„Save data – %s“,
    `SavesBackup` „Sichern“/„Back up“, `SavesRestore` „Wiederherstellen“/„Restore“, `SavesDelete` „Sicherung
    löschen“/„Delete backup“, `SavesEmpty` „Noch kein Spielstand“/„No save data yet“, `SavesNandLocked` „NAND
    gesperrt – Neustart über den Homebrew Channel“/„NAND locked – restart from the Homebrew Channel“,
    `SavesUnknown` „Speicherort unbekannt (emulators.ini: save_dir)“/„Save location unknown (emulators.ini:
    save_dir)“, `SavesConfirm` „Sicherung vom %s zurückspielen? Der jetzige Stand wird vorher gesichert.“/„Restore
    the backup from %s? The current save is backed up first.“, `SavesDone`/`SavesFailed` „Fertig“/„Done“,
    „Fehlgeschlagen: %s“/„Failed: %s“, `SavesNoBackup` „Keine Sicherungen“/„No backups“.
- [ ] Tests (FAIL): Details zeigt Zeile nur mit `setHasSave(true)` und Fokus erreicht sie; A auf Sichern →
      `BackupSave`; Wiederherstellen ohne Sicherung gesperrt; `NandLocked` zeigt nur Text; Hoch/Runter wählt
      die Sicherung; Strings in beiden Sprachen nicht leer.
- [ ] Umsetzen, PASS, Commit `feat: Szene Spielstand im Spieldetail`.

### Task 11: Verdrahtung in `main.cpp`
**Files:** `src/app/main.cpp`, `src/core/config.{hpp,cpp}` (Debug-Schlüssel), `tests/host/core/config_test.cpp`
- Consumes: alles oben.
- [ ] Debug-Schlüssel (Test zuerst in `config_test`): `save_backup = <spielschlüssel>`, `save_restore = <spielschlüssel>|latest`
      (`latest` = neueste Sicherung des zuletzt gesicherten Spiels in derselben Sitzung).
- [ ] Beim Start nach dem Datenordner: `recoverRestore` → Marker `NL_SAVE_RECOVER none|rollback|failed`.
- [ ] Vor dem ersten NAND-Zugriff `applyIsfsPermissionPatch()` einmal je Sitzung → `NL_IOSPATCH <name>`.
- [ ] `OpenSaves`: `preflightGame` ohne Start (nur Ziel, kein Commit), `saveQueryFor` → `locateSave` →
      `readUnit` (Größe/Anzahl) → `SavesScene::open`. NAND gesperrt → Zustand `NandLocked`.
- [ ] `BackupSave`/`RestoreSave` (nach `ChoiceDialog`)/`DeleteBackup` → `SaveWorker`; Ergebnis →
      `NL_SAVE_BACKUP ok|empty|fail <unit>` bzw. `NL_SAVE_RESTORE ok|fail <unit>`, Szene aktualisieren.
- [ ] Wii-Build mit/ohne TLS, 0 Warnungen. Commit `feat: Save-Manager im Launcher verdrahten`.

### Task 12: Smokes, Abnahme, Doku
**Files:** `tools/dolphin_smoke.py`, `tools/tests/test_dolphin_smoke.py`, `docs/acceptance/m26-wii.md`, `docs/STATUS.md`,
`docs/superpowers/spikes/m26-cloud-spielstaende.md`
- [ ] `--save-fixture nand|emunand|gb`: legt im Dev-User (`../wii_tools/dolphin-user`) bzw. auf der virtuellen SD
      eine Test-Einheit mit bekannten Bytes an (selbst erzeugt, keine Spieldaten) und räumt danach auf; Tool-Test zuerst.
- [ ] Smokes: `save_backup` je Fixture → `NL_SAVE_BACKUP ok`, ZIP mit `zipfile` geprüft (Meta, Hash); geänderte Datei +
      `save_restore=latest` → `NL_SAVE_RESTORE ok`, Datei wieder gleich; vorbereitetes Journal → `NL_SAVE_RECOVER rollback`.
- [ ] Spike S3 beantworten (schreibt Dolphin `/title/…/data` aus einer DOL?) und ins Spike-Dokument; bei Nein die
      NAND-Smokes auf EmuNAND beschränken.
- [ ] `docs/acceptance/m26-wii.md`: Hardware-Punkte „ungeprüft“ (S1/S2, Sichern/Wiederherstellen echter NAND
      mit vorheriger Rückfrage und BootMii-Backup, Spiel speichert danach selbst, Nintendont, Snes9x GX, VBA GX, EmuNAND).
- [ ] STATUS.md: M26 eingetragen. Host-Tests, Tool-Tests, beide Builds, Lizenzprüfung grün.
- [ ] Commit `docs: Abnahme und Stand M26`, PR, CI, Merge.
