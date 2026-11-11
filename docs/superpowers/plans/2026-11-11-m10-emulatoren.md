# M10 Emulator-Verwaltung Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Launcher installiert und aktualisiert die Emulatoren (fceugx, snes9xgx, vbagx, not64, wii64, Nintendont) selbst aus dem Open Shop Channel; dasselbe kann `wii_deploy.py --emulators` am PC.

**Architecture:** Die gesamte Logik (CRC32, Inflate, ZIP-Leser, OSC-Listenparser, Katalog und Status, Installationsablauf) liegt plattformfrei in `core/` und läuft im Host-Test gegen `MemFileSystem` und eine Download-Attrappe. Die Plattform liefert nur einen streamenden HTTP-Download in eine Datei, den freien Platz und einen Worker-Thread. Die UI ist eine neue Overlay-Szene hinter Einstellungen → „Emulatoren“.

**Tech Stack:** C++20 (ohne Exceptions/RTTI), libogc/LWP, doctest, Python 3 (`zipfile`, `zlib` nur in Tests/Tools), Dolphin-Smoke mit lokalem HTTP-Server.

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md` – 6.9 (neu), 2, 3.4, 4.1, 4.3, 5.1, 5.4, 9 (M10), 10.

**Basis-Branch:** `feat/m10-emulatoren` von `main` (279e5c8), Spec-Commit 56b0508.

## Global Constraints

- `core/` ohne `#include <ogc/...>`; alles in `core/` testgetrieben (doctest), Wii-Build warnungsfrei.
- Keine Exceptions, kein RTTI; fehlbare Funktionen liefern `Result<T>`.
- Quelle: `config.ini` `[emulators] repo`, Standard `http://hbb1.oscwii.org/hbb`; `[debug] emu_repo` ersetzt sie (Smoke).
- Liste `<repo>/listv036.txt`, Paket `<repo>/<name>/<name>.zip` (Groß-/Kleinschreibung wie in der Liste).
- Kein Netzzugriff beim Start; Liste nur auf Aktion.
- Platzbedarf: ZIP-Größe + entpackte Größe + 1 MiB Reserve.
- ZIP: nur Methoden 0 und 8; absolute Pfade, `..`, `\`, `:` → ganzes Paket ungültig.
- Unter `apps/` ersetzen, außerhalb nur fehlende Dateien anlegen; `apps/<x>/boot.dol` zuletzt.
- `sd:/newlauncher/emulators_installed.ini`: Abschnitt pro OSC-Name, `timestamp`, `version`; nur nach Erfolg schreiben.
- Zwischenablage `sd:/newlauncher/tmp/`, nach jeder Installation und beim Start geleert.
- Alle neuen Texte DE und EN.
- Keine ROMs, ISOs, WADs im Repo; Test-ZIPs entstehen im Test bzw. im Smoke-Skript.

## Review Focus

1. Bösartige oder kaputte ZIP-Einträge (`../`, `/abs`, `C:`, `\`, Methode 12, CRC falsch, Größe gelogen, abgeschnitten): Paket abgelehnt, nichts außerhalb von `tmp/` geschrieben, kein Absturz, keine Riesen-Allokation.
2. Abbruch mitten in der Installation (B, Netz weg, Karte voll beim Verschieben): alte `boot.dol` bleibt startbar oder die neue App ist vollständig; `tmp/` wird beim nächsten Start geräumt; `emulators_installed.ini` lügt nicht.
3. Kaputte oder fremde OSC-Liste (Kopfzeile fehlt, Block abgeschnitten, Zahlen kaputt, CRLF/LF gemischt, Katalog-App fehlt): betroffene Einträge „nicht im Repo“, Rest funktioniert.
4. Nutzerdaten außerhalb von `apps/` (Saves, `fceugx/fceugx.cfg`, Speicherkarten) bleiben unverändert; Dateien unter `apps/` (meta.xml, icon.png) werden ersetzt.
5. UI während eines 15-MB-Downloads: bedienbar, Fortschritt sichtbar, Szene schließen während des Laufs, HOME → Homebrew Channel während des Laufs (Thread wird sauber beendet, Laufwerke erst danach ausgehängt).

---

### Task 1: CRC32 und Inflate

**Files:**
- Create: `src/core/crc32.hpp`, `src/core/crc32.cpp`, `src/core/inflate.hpp`, `src/core/inflate.cpp`
- Create: `tools/gen_deflate_samples.py` (erzeugt `tests/host/support/deflate_samples.hpp`), `tests/host/support/deflate_samples.hpp`
- Create: `tests/host/core/crc32_test.cpp`, `tests/host/core/inflate_test.cpp`
- Modify: `CMakeLists.txt` (Quellen, Tests)

**Interfaces:**
- Produces: `u32 nl::crc32(std::span<const u8> data, u32 crc = 0);` (zlib-kompatibel, fortsetzbar)
- Produces: `Result<std::vector<u8>> nl::inflateRaw(std::span<const u8> in, std::size_t outSize);` – roher Deflate-Strom (RFC 1951, ohne zlib-Kopf). `outSize` ist die erwartete Länge; mehr oder weniger Ausgabe, fehlerhafte Codes, Distanz vor den Anfang, Eingabe zu Ende vor dem letzten Block → `ErrorCode::InvalidData`.

- [ ] **Step 1: Testdaten erzeugen.** `gen_deflate_samples.py` schreibt mit `zlib.compressobj(level, zlib.DEFLATED, -15)` C++-Arrays: `kStored` (level 0, 300 B), `kFixed` (kurzer Text, `Z_FIXED`-Strategie), `kDynamic` (20 KB deterministischer Text mit Wiederholungen, level 9), `kMultiBlock` (200 KB Pseudozufall aus festem Seed mit Wiederholungen, damit mehrere Blöcke entstehen), jeweils mit Klartext-Länge und CRC32 aus `zlib.crc32`.
- [ ] **Step 2: Failing tests.** `crc32_test.cpp`: `crc32("123456789") == 0xCBF43926`, leer → 0, fortgesetzt in zwei Teilen = in einem. `inflate_test.cpp`: jede Probe ergibt Länge und CRC wie erzeugt; `kDynamic` mit `outSize-1` und `outSize+1` → InvalidData; abgeschnittene Eingabe (halbe Länge) → InvalidData; Block-Typ 3 (`{0x07}`) → InvalidData; Distanz vor den Anfang (fester Block, Länge 3, Distanz 1 als erstes Symbol) → InvalidData.
- [ ] **Step 3:** Tests laufen lassen. Expected: FAIL (Kompilierfehler, Header fehlen).
- [ ] **Step 4: Implementieren.** CRC32 tabellenbasiert (Polynom 0xEDB88320). Inflate: Bit-Leser LSB-zuerst, kanonische Huffman-Tabellen (Zähl-/Symbol-Tabellen wie in zlibs `puff`), Ausgabe in vorab reservierten `outSize`-Puffer; jede Schreibstelle prüft die Grenze.
- [ ] **Step 5:** `bht.ps1 "*crc32*,*inflate*"` → PASS; ganze Host-Suite grün.
- [ ] **Step 6: Commit** `feat(core): CRC32 und Inflate für Emulator-Pakete`

### Task 2: ZIP-Leser

**Files:**
- Create: `src/core/zip_reader.hpp`, `src/core/zip_reader.cpp`, `tests/host/core/zip_reader_test.cpp`, `tests/host/support/zip_builder.hpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `crc32`, `inflateRaw`; `FileSystem::read`.
- Produces:
  ```cpp
  struct ZipEntry { std::string name; u16 method; u32 crc; u32 compSize; u32 size; u32 localOffset; bool isDir; };
  bool isSafeZipPath(std::string_view name);               // relativ, kein "..", kein "\\", kein ":", nicht leer
  Result<std::vector<ZipEntry>> readZipDirectory(FileSystem& fs, std::string_view zip, u64 zipSize);
  Result<std::vector<u8>> extractZipEntry(FileSystem& fs, std::string_view zip, const ZipEntry& e);
  inline constexpr u32 kMaxZipEntryBytes = 32u * 1024 * 1024;
  inline constexpr u32 kMaxZipEntries = 4096;
  ```
- `zip_builder.hpp` (nur Test): `std::vector<u8> buildZip(const std::vector<TestZipEntry>&)` mit `TestZipEntry{name, data, deflated}`; für deflated nimmt er eine Probe aus `deflate_samples.hpp` samt Länge und CRC.

- [ ] **Step 1: Failing tests.** Verzeichnis mit 3 Einträgen (Ordner `apps/x/`, stored `apps/x/meta.xml`, deflate `apps/x/boot.dol` = `kDynamic`) wird gelesen, Namen, Größen, `isDir`; `extractZipEntry` liefert die Bytes. Ablehnungen: kein End-of-Central-Directory, EOCD mit Kommentar (wird gefunden), Eintragszahl > `kMaxZipEntries`, Methode 12, `size > kMaxZipEntryBytes`, falsche CRC, lokaler Kopf mit falscher Signatur, Daten hinter Dateiende. `isSafeZipPath`: `"apps/a/boot.dol"` ja; `"../x"`, `"a/../../x"`, `"/x"`, `"C:/x"`, `"a\\b"`, `""` nein. `readZipDirectory` lehnt das ganze Archiv ab, sobald ein Name unsicher ist (`ErrorCode::InvalidData`, Kontext = Name).
- [ ] **Step 2:** Expected: FAIL (Kompilierfehler).
- [ ] **Step 3: Implementieren.** EOCD in den letzten 65 557 Bytes rückwärts suchen; Zentralverzeichnis in einem `read`; ZIP64 und Verschlüsselung (Flag-Bit 0) → InvalidData. `extractZipEntry` liest den lokalen Kopf (Namens- und Extra-Länge von dort), dann die Daten, inflated bzw. kopiert, prüft Größe und CRC.
- [ ] **Step 4:** `bht.ps1 "*zip*"` → PASS; Suite grün.
- [ ] **Step 5: Commit** `feat(core): ZIP-Leser mit Pfadprüfung`

### Task 3: OSC-Liste, Katalog und Status

**Files:**
- Create: `src/core/emu_catalog.hpp`, `src/core/emu_catalog.cpp`, `tests/host/core/emu_catalog_test.cpp`
- Modify: `src/core/config.hpp/.cpp` (`emulatorRepo`, `debugEmuRepo`, `debugEmuInstall`), `tests/host/core/config_test.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct OscApp { std::string name; u32 timestamp = 0; u32 unzippedSize = 0; u32 zipSize = 0; std::string displayName, version; };
  std::vector<OscApp> parseOscList(std::string_view text);   // überspringt Kopf, Kategorien, kaputte Blöcke
  struct EmuInfo { std::string_view oscName; std::string_view label; std::string_view dol; };
  std::span<const EmuInfo> emulatorCatalog();                // Reihenfolge wie Spec 6.9
  struct InstalledEmu { u32 timestamp = 0; std::string version; };
  std::map<std::string, InstalledEmu> parseInstalledEmus(std::string_view ini);
  std::string formatInstalledEmus(const std::map<std::string, InstalledEmu>&);
  enum class EmuState : u8 { NotInstalled, Current, UpdateAvailable, UnknownVersion };
  struct EmuRow { const EmuInfo* info; EmuState state; std::optional<InstalledEmu> installed; std::optional<OscApp> remote; };
  // remote leer = Liste noch nicht geladen oder App fehlt darin.
  std::vector<EmuRow> emuRows(const std::map<std::string, InstalledEmu>& installed,
                              const std::function<bool(std::string_view dol)>& dolExists,
                              std::span<const OscApp> list);
  std::string oscListUrl(std::string_view repo);
  std::string oscZipUrl(std::string_view repo, std::string_view name);
  inline constexpr std::string_view kDefaultEmuRepo = "http://hbb1.oscwii.org/hbb";
  inline constexpr std::string_view kInstalledEmusPath = "sd:/newlauncher/emulators_installed.ini";
  ```
- Config: `std::string emulatorRepo` (aus `[emulators] repo`, leer → `kDefaultEmuRepo`), `std::string debugEmuRepo`, `std::string debugEmuInstall` (Komma-Liste von OSC-Namen).

- [ ] **Step 1: Failing tests.** Fixture-Text im Test: Kopfzeile `Homebrew 2092896 v0.3.9e | …`, `=Emulators=`, die echten Blöcke von `fceugx` (`fceugx 1793664000 8367 5065728 dol 2849329 56463 0 wwwwngcs /fceugx;/apps/fceugx; . .`, Version `4.0.4`) und `Nintendont` (`… 1482019200 1156 1648128 dol 1077171 …`, Version `Newer than 4.431`), mit CRLF. Erwartet: 2 Apps, Felder korrekt. Ein Block mit nicht-numerischem Zeitstempel wird übersprungen, der folgende Block trotzdem gelesen; abgeschnittener letzter Block wird verworfen; LF statt CRLF geht auch. Status: DOL fehlt → NotInstalled; DOL da, kein Eintrag → UnknownVersion; Eintrag-Zeitstempel < Liste → UpdateAvailable; gleich → Current; Liste leer → Current/UnknownVersion/NotInstalled nur nach lokalem Stand. `formatInstalledEmus` → `parseInstalledEmus` Round-Trip. URLs: `oscZipUrl("http://h/hbb/", "Nintendont") == "http://h/hbb/Nintendont/Nintendont.zip"`. Config: `[emulators] repo` wird gelesen; fehlt → Standard.
- [ ] **Step 2:** Expected: FAIL.
- [ ] **Step 3:** Implementieren. Block-Erkennung: Zeile, deren erstes Token mit einem Katalog- oder beliebigem Namen beginnt und die mindestens 6 Felder mit numerischem 2., 4. und 6. Feld hat, startet einen 7-Zeilen-Block.
- [ ] **Step 4:** Host-Suite grün.
- [ ] **Step 5: Commit** `feat(core): OSC-Liste, Emulator-Katalog und Status`

### Task 4: Installationsablauf

**Files:**
- Create: `src/core/emu_install.hpp`, `src/core/emu_install.cpp`, `tests/host/core/emu_install_test.cpp`
- Modify: `src/core/fs.hpp` (`removeFile`, `renameFile`), `tests/host/support/mem_fs.hpp`, `src/platform/std_fs.hpp/.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 (`readZipDirectory`, `extractZipEntry`), Task 3 (`OscApp`, `InstalledEmu`, `parseInstalledEmus`, `formatInstalledEmus`, `oscZipUrl`).
- Produces:
  ```cpp
  // FileSystem (neu, rein virtuell): Datei löschen (fehlt → ok); umbenennen, Ziel darf nicht existieren.
  virtual Result<void> removeFile(std::string_view file) = 0;
  virtual Result<void> renameFile(std::string_view from, std::string_view to) = 0;

  struct EmuInstallIo {
      std::function<HttpOutcome(const std::string& url, const std::string& destFile, u32 maxBytes)> download;
      std::function<u64()> freeBytes;             // SD
      std::function<bool()> cancelled;            // zwischen allen Schritten geprüft
      std::function<void(std::string line)> log;
      std::function<void(EmuStep step, u32 done, u32 total)> progress;
  };
  enum class EmuStep : u8 { Download, Verify, Extract, Move };
  enum class EmuInstallError : u8 { None, NoSpace, Download, BadSize, BadZip, Write, Cancelled };
  struct EmuInstallResult { EmuInstallError error = EmuInstallError::None; std::string detail; };
  inline constexpr std::string_view kEmuTmpDir = "sd:/newlauncher/tmp";
  EmuInstallResult installEmulator(FileSystem& fs, EmuInstallIo& io, std::string_view repo, const OscApp& app);
  void clearEmuTmp(FileSystem& fs);           // rekursiv, beim Start und nach jeder Installation
  ```
- Marker (über `io.log`): `NL_EMU_INSTALL <name> ok files=<n>` bzw. `NL_EMU_INSTALL <name> fail <fehler> <detail>`.

- [ ] **Step 1: Failing tests** (MemFs, `download`-Attrappe schreibt ein mit `buildZip` erzeugtes Archiv nach `destFile`):
  - Erfolg: `apps/fceugx/boot.dol`, `apps/fceugx/meta.xml`, `fceugx/fceugx.cfg` landen unter `sd:/`; `emulators_installed.ini` hat `timestamp`/`version`; `tmp/` leer; Marker `ok files=3`.
  - Vorhandene `sd:/fceugx/fceugx.cfg` bleibt unverändert; vorhandene `sd:/apps/fceugx/meta.xml` wird ersetzt.
  - Reihenfolge: die letzte `renameFile` mit Ziel unter `apps/` ist `apps/fceugx/boot.dol` (MemFs protokolliert Umbenennungen).
  - `freeBytes` zu klein → NoSpace, kein Download-Aufruf.
  - Download liefert Failed → Download; Datei mit falscher Größe → BadSize; ZIP mit `../evil` → BadZip und `sd:/evil` existiert nicht; falsche CRC eines Eintrags → BadZip und **keine** Datei außerhalb von `tmp/` geändert.
  - `cancelled()` wird nach dem Download wahr → Cancelled, nichts verschoben, `tmp/` leer, `emulators_installed.ini` unverändert.
  - `renameFile` schlägt beim Verschieben fehl (MemFs `failRenameAt`) → Write; `emulators_installed.ini` unverändert.
  - Vorhandener Eintrag eines anderen Emulators in `emulators_installed.ini` bleibt erhalten.
- [ ] **Step 2:** Expected: FAIL.
- [ ] **Step 3: Implementieren.** Ablauf wie Spec 6.9 Schritte 1–5: Platz (`zipSize + unzippedSize + 1 MiB`), Download nach `tmp/<name>.zip` (maxBytes = `zipSize`), Größe über `list(tmp)` prüfen, Verzeichnis lesen, alle Einträge nach `tmp/<name>/<pfad>` entpacken (je Eintrag `writeAtomic`), dann verschieben: Ziel unter `apps/` → `removeFile` + `renameFile`; sonst nur wenn Ziel fehlt; `boot.dol`-Ziele am Schluss. `StdFileSystem::removeFile`/`renameFile` über `std::remove`/`std::rename`.
- [ ] **Step 4:** Host-Suite grün; Wii-Build warnungsfrei (StdFs).
- [ ] **Step 5: Commit** `feat(core): Emulator-Installation mit Zwischenablage`

### Task 5: Streaming-Download und Emu-Worker

**Files:**
- Modify: `src/core/http.hpp/.cpp` (Body-Senke), `tests/host/core/http_test.cpp`, `src/platform/http_client.hpp/.cpp`
- Create: `src/platform/emu_worker.hpp`, `src/platform/emu_worker.cpp`, `src/platform/sd_space.hpp`, `src/platform/sd_space.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `HttpParser::setSink(std::function<bool(std::span<const u8>)> sink)` – Body-Bytes gehen an die Senke statt in `body()`; `false` von der Senke → `State::Error`; `maxBody` gilt weiter (gezählt).
- Produces: `HttpOutcome platform::httpDownload(const Url&, const std::string& destFile, u32 maxBody, const volatile bool* cancel, volatile u32* received);` – schreibt per `fopen`/`fwrite` (64-KB-Puffer), löscht die Datei bei jedem Fehler.
- Produces: `u64 platform::sdFreeBytes();` (`statvfs("sd:/")`).
- Produces:
  ```cpp
  class EmuWorker {  // ein Auftrag zugleich, wie NetWorker
  public:
      bool startFetchList(std::string repo);
      bool startInstall(FileSystem& fs, std::string repo, std::vector<OscApp> apps);
      bool running() const; void cancel(); void join();
      std::optional<std::vector<OscApp>> takeList();          // nach FetchList; leer optional = noch nichts
      std::vector<std::pair<std::string, EmuInstallResult>> takeResults();
      EmuStep step() const; u32 done() const; u32 total() const; std::string currentName() const;
      std::vector<std::string> takeLogLines();
      bool listFailed() const;
  };
  ```
  Liste: `httpGet` mit `maxBody` 1 MiB, Marker `NL_EMU_LIST ok n=<anzahl>` bzw. `NL_EMU_LIST fail`. `net_init` einmal pro Lauf wie `NetWorker`.

- [ ] **Step 1: Failing test** `http_test.cpp`: Parser mit Senke, chunked und mit Content-Length – Senke erhält genau den Body, `body()` bleibt leer; Senke liefert `false` → Error; Body über `maxBody` → Error.
- [ ] **Step 2:** Expected: FAIL.
- [ ] **Step 3:** Senke implementieren; Plattformteile schreiben (nicht host-testbar, Abdeckung durch Task 7).
- [ ] **Step 4:** Host-Suite grün; `python tools/build_wii.py` warnungsfrei.
- [ ] **Step 5: Commit** `feat(platform): Streaming-Download und Emu-Worker`

### Task 6: Szene „Emulatoren“ und Verdrahtung

**Files:**
- Create: `src/ui/emulators_scene.hpp`, `src/ui/emulators_scene.cpp`, `tests/host/ui/emulators_scene_test.cpp`
- Modify: `src/core/settings_model.hpp/.cpp` (`SettingRow::Emulators` nach `VcMap`, `isAction`), `src/ui/settings_scene.cpp`, `src/ui/scene.hpp` (`AppCommand::OpenEmulators`, `EmuFetchList`, `EmuInstall`, `EmuInstallAll`, `EmuCancel`), `src/ui/strings.cpp`, `src/app/main.cpp`, zugehörige Tests

**Interfaces:**
- Consumes: Task 3 (`EmuRow`, `emuRows`), Task 5 (`EmuWorker`).
- Produces:
  ```cpp
  class EmulatorsScene final : public Scene {  // Overlay wie SettingsScene
  public:
      void setRows(std::vector<EmuRow> rows);
      void setBusy(std::optional<EmuProgress> p);   // EmuProgress{name, step, done, total}
      void setListState(ListState s);               // NotLoaded, Loading, Loaded, Failed
      void setError(std::string_view oscName, EmuInstallError e);
      int focus() const;
      const EmuRow* selectedRow() const;
  };
  ```
  Zeilen: je Emulator eine Zeile (Name, installiert, verfügbar, Status-Text), darunter Aktionszeilen „Nach Updates suchen“ und „Alle aktualisieren“. A auf Emulatorzeile → `EmuInstall` (nur wenn Liste geladen und App im Repo); A auf „Alle aktualisieren“ → `EmuInstallAll` (alle Zeilen mit UpdateAvailable oder UnknownVersion, die im Repo sind; nicht installierte bleiben unberührt); B während Arbeit → `EmuCancel`, sonst schließen. Kein Netz beim Öffnen.
- main: Start → `clearEmuTmp`; Befehle an `EmuWorker`; nach Installationsende `emulators_installed.ini` neu lesen, Zeilen neu berechnen, `not64Dol`/`wii64Dol` neu bestimmen (die Lambdas lesen dann die neuen Werte); `perf->frame(..., busy)` zählt `emu.running()` mit; Beenden/HOME → `emu.cancel(); emu.join()` vor dem Aushängen. Debug: `debugEmuRepo` ersetzt `emulatorRepo`; `debugEmuInstall` nicht leer → nach `NL_READY` Liste laden, danach die genannten Apps installieren; `debug autostart` wartet, bis diese Installation fertig ist.
- Texte DE/EN: „Emulatoren“/„Emulators“, „Nach Updates suchen“/„Check for updates“, „Alle aktualisieren“/„Update all“, Status „nicht installiert“/„not installed“, „aktuell“/„up to date“, „Update verfügbar“/„update available“, „Version unbekannt“/„unknown version“, „nicht im Repository“/„not in repository“, Schritte „Lade …“/„Downloading …“, „Prüfe …“/„Verifying …“, „Entpacke …“/„Extracting …“, „Installiere …“/„Installing …“, Fehler je `EmuInstallError` (u. a. „Zu wenig Platz auf der SD-Karte“/„Not enough space on the SD card“).

- [ ] **Step 1: Failing tests** `emulators_scene_test.cpp`: 6 Emulatorzeilen + 2 Aktionen; A auf Zeile vor geladener Liste → kein Befehl; nach `setListState(Loaded)` und Remote vorhanden → `EmuInstall`; „Alle aktualisieren“ → `EmuInstallAll`; B während `setBusy` → `EmuCancel`, Szene bleibt; B ohne Arbeit → schließt; Zeiger-Treffer wählt Zeile. `settings_scene_test`: Zeile „Emulatoren“ → `OpenEmulators`. `strings_test`: alle neuen Schlüssel in DE und EN nicht leer.
- [ ] **Step 2:** Expected: FAIL.
- [ ] **Step 3:** Implementieren, main verdrahten.
- [ ] **Step 4:** Host-Suite grün; Wii-Build warnungsfrei; bestehende Smokes (`accept7.sh`) grün.
- [ ] **Step 5: Commit** `feat(ui): Emulatoren installieren und aktualisieren`

### Task 7: Dolphin-Smoke mit Emulator-Server

**Files:**
- Modify: `tools/dolphin_smoke.py`, `tools/tests/test_dolphin_smoke.py`, `README.md`, `docs/hardware-acceptance.md`

**Interfaces:**
- Consumes: Marker aus Task 4/5, Debug-Schlüssel aus Task 6.
- Produces: `--emu-server`: lokaler Server liefert `/hbb/listv036.txt` (Kopfzeile + Block `fceugx` mit Zeitstempel 1793664000, korrekter `zipSize` und `unzippedSize`) und `/hbb/fceugx/fceugx.zip` (mit `zipfile` erzeugt: `apps/fceugx/boot.dol` = `argv_echo.dol`, `apps/fceugx/meta.xml`, `fceugx/fceugx.cfg`); vor dem Start im SD-Sync-Ordner `apps/fceugx/` löschen und `[nes] dol` in `newlauncher/emulators.ini` auf `sd:/apps/fceugx/boot.dol` setzen; Debug-Zeilen `emu_repo = http://127.0.0.1:<port>/hbb`, `emu_install = fceugx`.

- [ ] **Step 1: Failing tool tests:** `write_emu_server_files` erzeugt Liste und ZIP, deren Größen in der Liste stimmen und deren ZIP die drei Einträge enthält; `--emu-server` setzt die Debug-Zeilen und ändert `emulators.ini` nur im Sync-Ordner (nicht in `tests/fixtures`).
- [ ] **Step 2:** Expected: FAIL. **Step 3:** implementieren. **Step 4:** `python -m unittest discover -s tools/tests` OK.
- [ ] **Step 5: Dolphin:** `timeout 300 python tools/dolphin_smoke.py --emu-server --autostart <NES-Fixture-Schlüssel> --marker "NL_EMU_LIST ok" --marker "NL_EMU_INSTALL fceugx ok" --marker "ARGV"` → `SMOKE OK`. Danach `accept7.sh` komplett grün.
- [ ] **Step 6:** README (neue Marker, `--emu-server`, Einstellung `[emulators] repo`), Abnahme-Checkliste: Punkt „E1 Emulatoren auf echter Wii installieren/aktualisieren (WLAN)“.
- [ ] **Step 7: Commit** `test(smoke): Emulator-Installation in Dolphin`

### Task 8: `wii_deploy.py --emulators`

**Files:**
- Modify: `tools/wii_deploy.py`, `tools/tests/test_wii_deploy.py`, `README.md`

**Interfaces:**
- Produces: `--emulators [NAME ...]` (ohne Namen: alle aus dem Katalog), `--repo URL` (Standard wie Spec), `--yes` (ohne: Liste der Pakete mit Größe zeigen und auf `j` warten). Funktionen `parse_osc_list(text) -> dict[str, OscApp]`, `install_emulator(sd: Path, zip_path: Path, app) -> int` (gleiche Regeln: sichere Pfade, `apps/` ersetzen, sonst nur fehlende, `boot.dol` zuletzt, `emulators_installed.ini` aktualisieren), Download über `urllib.request` in ein Temp-Verzeichnis.
- Katalog-Namen in Python identisch zu `emulatorCatalog()` (Test vergleicht gegen eine aus `emu_catalog.cpp` gelesene Liste).

- [ ] **Step 1: Failing tests** (ohne Netz, `urlopen` gemockt): Listen-Parsing wie Task 3; Installation auf Temp-Karte mit gleicher Erwartung wie Task 4 (Ersetzen/Behalten/unsichere Pfade/CRC-Fehler über `zipfile.BadZipFile`); `--yes` fehlt und Eingabe „n“ → nichts geladen; `emulators_installed.ini`-Format identisch zum C++-Format (gleiche Beispieldatei).
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Tool-Tests OK.
- [ ] **Step 5: Commit** `feat(tools): Emulatoren per wii_deploy installieren`
