# M2 – Bibliothek: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Launcher findet die echten Spiele auf SD bzw. USB, merkt sie sich in `index.bin`, zeigt sie sofort beim Start und hält sie per Hintergrund-Scan aktuell; dazu Favoriten, Verlauf, A–Z-Sprung, Suche mit Bildschirmtastatur und Detailansicht.

**Architecture:** Alles Fachliche liegt plattformfrei in `core/` und wird gegen ein `FileSystem`-Interface mit In-Memory-Implementierung getestet: INI-Parser und Konfiguration, Titelbereinigung und Suche, Big-Endian-Dateiformate (`index.bin`, `meta.bin`), Disc-Header, Scanner, Bibliotheks-Speicher mit Favoriten und Verlauf. Die Wii-Seite bekommt nur Mount (libfat), eine stdio-Implementierung von `FileSystem`, einen LWP-Scan-Thread und die Verdrahtung in `main`. Neue UI-Szenen (Start/USB-Dialog, A–Z-Leiste, Tastatur, Details) folgen dem Szenen-Muster aus M1 und sind host-getestet.

**Tech Stack:** wie M1, zusätzlich libfat (`fat`), LWP-Threads/Mutex aus libogc 3.2.1, Python-stdlib für Fixture- und `meta.bin`-Werkzeuge.

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md` (§3.2, §3.5, §4.1–4.4, §5, §6.1–6.3, §8.2–8.3, §9 M2)

## Global Constraints

- Alle Global Constraints aus dem M1-Plan gelten weiter (C++20 ohne Exceptions/RTTI, `Result`, keine Allokation pro Frame im Leerlauf, Latin-1 + „…", Dolphin nur mit `-u`, keine ROMs/ISOs/WADs außer Dummy-Fixtures unter `tests/fixtures/`).
- Allokationen sind erlaubt beim Laden, Scannen, Speichern und beim Wechsel von Liste/Filter – nie im Leerlauf (`NL_ALLOC_IDLE 0` bleibt Pflicht).
- Binärformate Big-Endian, feldweise; Strings `u16` Länge + UTF-8 (Spec §5.5). Unbekannte Version oder kaputter Header → Datei verwerfen, neu erzeugen.
- INI-Regeln Spec §5.4 wörtlich: Schlüssel vor dem **ersten** `=`, beides getrimmt, Kommentare nur als ganze Zeile mit `;` oder `#`, keine Zeilenend-Kommentare.
- Pfade: Konfiguration auf `sd:/newlauncher/`, Cache auf `<lib>:/newlauncher/cache/`; `<lib>` = `usb` oder `sd` laut `config.ini`.
- Spiel-Schlüssel laut Spec §5.2: Wii/GC = ID6, ROM = `<konsole>/<dateiname>`.
- Die UI-Datenstrukturen gehören ausschließlich dem Haupt-Thread (Spec §4.3); der Scan-Thread liefert ein fertiges Ergebnis über einen Mutex-geschützten Platz.
- Commits enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Neue Log-Marker: `NL_CONFIG <ok|default>`, `NL_MOUNT <lib>`, `NL_LIBRARY games=<n> source=<index|scan>`, `NL_SCAN_DONE games=<n> added=<a> removed=<r>`, `NL_INDEX_SAVED`, `NL_FAV_SAVED`.

## Review Focus

1. **Kaputte oder fremde Dateien in der Bibliothek** (abgeschnittene `.iso`, `.wbfs` ohne gültigen Header, 0-Byte-ROM, Unterordner ohne Spieldatei) – erwartet: Datei wird übersprungen bzw. mit Dateinamen-Titel aufgenommen, kein Absturz, Scan läuft weiter (Tests in Task 4 und 5).
2. **Beschädigter Cache** (`index.bin`/`meta.bin` abgeschnitten, falsches Magic, Längenfeld größer als Rest der Datei) – erwartet: verworfen, Neuaufbau per Scan, nie Lesen hinter den Puffer (Test in Task 3).
3. **Sonderzeichen in Dateinamen** (`;`, `=`, `[`, Umlaute, sehr lange Namen) in Favoriten/Verlauf/Schlüsseln – erwartet: Round-Trip unverändert, kein Abschneiden am `=` im Wert (Tests in Task 1 und 6).
4. **A–Z auf Buchstaben ohne Titel** und Suche ohne Treffer – erwartet: Sprung zum nächsten folgenden Buchstaben mit Titeln bzw. Leertext „Keine Treffer", Fokus nie auf nicht vorhandener Kachel (Tests in Task 2, 10, 11).
5. **Bibliothek ändert sich während der Bedienung** (Hintergrund-Scan liefert neue Liste, fokussiertes Spiel gelöscht) – erwartet: Leisten-Eintrag und Fokus bleiben per Schlüssel erhalten, sonst Fokus 0 bzw. -1; Konsole ohne Spiele verschwindet und die Leiste fällt auf den nächsten Eintrag (Test in Task 8).

## Abweichungen und Festlegungen gegenüber der Spec

| Punkt | Festlegung in M2 | Grund |
|---|---|---|
| Inkrementeller Scan (§6.1/6.2) | Jeder Hintergrund-Scan listet **alle** Ordner; Disc-Header werden nur für Dateien gelesen, deren Pfad + Größe nicht schon im Index stehen. `mtime` wird im Index mitgeschrieben, aber nicht als einziges Kriterium genutzt | FAT aktualisiert die Ordner-mtime beim Hinzufügen von Dateien nicht zuverlässig; Verzeichnislisten sind billig, Header-Lesen ist der teure Teil |
| „Delta an UI" (§6.1) | Der Scan-Thread liefert die komplette neue Liste; die UI tauscht sie aus und erhält Leisten-Eintrag und Fokus per Schlüssel | einfacher, gleiches sichtbares Verhalten |
| `meta.bin` | Leser in M2 (Titel, Jahr, Spieler, Beschreibung); Writer nur als kleines Python-Modul für Fixtures, das `prepare_meta.py` (M5) wiederverwendet | Detailansicht braucht die Felder; das PC-Skript ist M5 |
| Starten | Knopf „Starten" in der Detailansicht ist sichtbar, zeigt in M2 den Hinweis „Starten folgt mit M3"; der Verlauf wird erst beim echten Start (M3) geschrieben | Backends sind M3/M4 |
| N64-Startmethode in der Detailansicht | nicht in M2 | M4 |
| Cover | weiter Platzhalter-Kacheln | M5 |
| `favorites.ini` | eine Zeile pro Schlüssel ohne `=`, Abschnitt `[favorites]`; `history.ini`: Abschnitt `[history]`, `<unixzeit> = <schlüssel>`, neueste zuerst, max. 20 | Spec §5.4 lässt das Zeilenformat offen |
| Log-Datei (§4.2) | Ringpuffer 16 KB, wird nach `NL_READY` und beim Beenden nach `sd:/newlauncher/log.txt` geschrieben (nicht im Absturz-Handler) | Schreiben im Exception-Handler ist zu riskant |

## Dateistruktur

```
src/core/
  ini.hpp/.cpp            IniDoc: parse, get, set, serialize (Spec 5.4)
  config.hpp/.cpp         Config + load/defaults/serialize
  text_fold.hpp/.cpp      cleanTitle, foldForSearch, letterBucket
  bin_io.hpp/.cpp         BeWriter, BeReader (fehlersicher, nie über das Ende)
  index_file.hpp/.cpp     IndexData ↔ index.bin
  meta_db.hpp/.cpp        MetaDb (meta.bin, binäre Suche)
  fs.hpp                  FileSystem-Interface, DirEntry
  disc_header.hpp/.cpp    parseWiiIso, parseWbfs, parseGcIso
  scanner.hpp/.cpp        scanLibrary
  library_store.hpp/.cpp  LibraryStore (besitzt Strings), Favoriten, Verlauf
src/platform/
  storage.hpp/.cpp        mountSd, mountUsb (mit Timeout-Schritt), unmountAll
  std_fs.hpp/.cpp         StdFileSystem (stdio/dirent)
  scan_worker.hpp/.cpp    LWP-Thread + Mutex-Ergebnisplatz
  log.cpp                 + Ringpuffer, flushLogFile()
src/ui/
  startup_scene.hpp/.cpp  „Warte auf USB …", Dialog „Erneut versuchen" / „SD verwenden", Scan-Fortschritt
  jump_bar.hpp/.cpp       A–Z-Overlay
  keyboard.hpp/.cpp       Bildschirmtastatur-Overlay
  details_scene.hpp/.cpp  Detailansicht
  main_scene.*, layout.*  erweitert: setLibrary, Suchmodus, Favorit-Taste, Toast
tools/
  metabin.py              write_meta_bin(path, entries) – auch für M5
  make_fixtures.py        erzeugt tests/fixtures/sd/ (Dummy-Header, leere ROMs, INIs, meta.bin)
tests/fixtures/sd/        erzeugte Fixture-Bibliothek (committet)
tests/host/support/mem_fs.hpp  MemFileSystem für Tests
```

Testbefehle wie in M1 (Host, Tools, Wii-Build, Smoke, Selbsttest).

---

### Task 1: INI-Parser und Konfiguration

**Files:**
- Create: `src/core/ini.hpp/.cpp`, `src/core/config.hpp/.cpp`
- Test: `tests/host/core/ini_test.cpp`, `tests/host/core/config_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl {
class IniDoc {                                  // Reihenfolge von Abschnitten und Zeilen bleibt erhalten
 public:
  static IniDoc parse(std::string_view text);   // tolerant: kaputte Zeilen werden übersprungen
  std::string_view get(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
  void set(std::string_view section, std::string_view key, std::string_view value);   // ersetzt oder hängt an
  std::span<const std::string> lines(std::string_view section) const;   // Zeilen ohne „=" (Favoriten)
  void addLine(std::string_view section, std::string_view line);
  std::string serialize() const;                // „[sec]\nkey = value\n", Abschnitte durch Leerzeile getrennt
};

struct Config {
  enum class Language : u8 { Auto, De, En };
  enum class LibraryDevice : u8 { Usb, Sd };
  Language language = Language::Auto;
  LibraryDevice library = LibraryDevice::Usb;
  int usbTimeoutSec = 15;
  ThemeMode themeMode = ThemeMode::Light;
  int darkFromMin = 19 * 60, darkToMin = 7 * 60;
  bool music = true; int musicVolume = 60; bool sounds = true;
  std::string coverRegion = "auto";
};
Config configFromIni(const IniDoc& ini);        // fehlende/ungültige Werte → Standard (Spec 5.4)
std::string defaultConfigText();                // exakt der Block aus Spec 5.4
int parseClock(std::string_view hhmm, int fallback);   // "19:00" → 1140; ungültig → fallback
}
```

- [ ] **Step 1: Tests schreiben.** Pflichtfälle:

```cpp
TEST_CASE("Schlüssel ist alles vor dem ersten =")        // "a = b = c" → key "a", value "b = c"
TEST_CASE("Ränder werden getrimmt, Leerzeichen im Schlüssel bleiben")   // "  Super Mario 64 (Europe).z64 = 0001 " 
TEST_CASE("nur ganze Zeilen mit ; oder # sind Kommentare")              // "rom;1.nes = x" → key "rom;1.nes"
TEST_CASE("Zeilen ohne = landen in lines() des Abschnitts")
TEST_CASE("CRLF, BOM am Anfang und fehlender Zeilenumbruch am Ende")
TEST_CASE("Werte vor dem ersten Abschnitt werden ignoriert, leere Abschnittsnamen ebenso")
TEST_CASE("set ersetzt vorhandenen Schlüssel und hängt neuen an; serialize/parse Round-Trip")
TEST_CASE("Umlaute und [ ] im Schlüssel überstehen den Round-Trip")
TEST_CASE("configFromIni: Standardwerte bei leerer Datei")              // Light, 1140, 420, Usb, 15
TEST_CASE("configFromIni: Werte aus defaultConfigText() == Standardwerte")
TEST_CASE("configFromIni: ungültige Werte fallen auf Standard")         // mode = blau, usb_timeout = -3, dark_from = 25:00
TEST_CASE("parseClock") // "07:00"→420, "7:5"→425, "24:00"→fallback, "ab"→fallback, ""→fallback
```

- [ ] **Step 2:** Host-Tests. Expected: Build schlägt fehl.
- [ ] **Step 3:** Implementieren. `usb_timeout` gültig 1–120; `music_volume` 0–100.
- [ ] **Step 4:** Host-Tests grün.
- [ ] **Step 5:** Commit `feat(core): INI-Parser und Konfiguration`.

---

### Task 2: Titelbereinigung, Suche, Anfangsbuchstabe

**Files:**
- Create: `src/core/text_fold.hpp/.cpp`
- Test: `tests/host/core/text_fold_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl {
std::string cleanTitle(std::string_view fileStem);   // ohne (…)- und […]-Gruppen, Leerzeichen zusammengefasst, getrimmt
std::string foldForSearch(std::string_view utf8);    // klein, Akzente weg (Latin-1: ä→a, ß→ss, é→e …), nicht-alphanumerisch → ' ', zusammengefasst
bool matchesQuery(std::string_view foldedTitle, std::string_view foldedQuery);   // alle Wörter der Anfrage als Teilstring, Reihenfolge egal
char letterBucket(std::string_view title);           // 'A'–'Z' nach Faltung, sonst '#'
int compareTitles(std::string_view a, std::string_view b);   // nach foldForSearch, dann bytewise; Artikel zählen mit (Spec 6.2)
}
```

- [ ] **Step 1: Tests schreiben.**

```cpp
TEST_CASE("cleanTitle entfernt No-Intro-Tags") {
  CHECK(cleanTitle("Super Mario 64 (Europe) (En,Fr,De)") == "Super Mario 64");
  CHECK(cleanTitle("Tetris (World) [!]") == "Tetris");
  CHECK(cleanTitle("Zelda (Rev 1) - Link's Awakening") == "Zelda - Link's Awakening");
  CHECK(cleanTitle("(Europe)") == "(Europe)");          // würde leer → Original behalten
  CHECK(cleanTitle("Kirby (unclosed") == "Kirby (unclosed");
}
TEST_CASE("foldForSearch") {
  CHECK(foldForSearch("Pokémon Snap") == "pokemon snap");
  CHECK(foldForSearch("Die Legende vom Äonentor") == "die legende vom aonentor");
  CHECK(foldForSearch("Straße") == "strasse");
  CHECK(foldForSearch("Mario Kart: Double Dash!!") == "mario kart double dash");
  CHECK(foldForSearch("\xC3") == "");                   // ungültiges UTF-8 → ignoriert
}
TEST_CASE("matchesQuery: Wörter in beliebiger Reihenfolge, leere Anfrage passt immer")
TEST_CASE("letterBucket") { CHECK(letterBucket("Ökoland") == 'O'); CHECK(letterBucket("1080° Snowboarding") == '#'); CHECK(letterBucket("") == '#'); }
TEST_CASE("compareTitles: Groß/klein egal, Artikel zählen") // "the legend" > "Super"; "a" < "B"
```

- [ ] **Step 2:** RED. **Step 3:** `decodeUtf8` aus `gfx/font.cpp` nach `core/utf8.hpp/.cpp` verschieben (`nl::decodeUtf8`, gleiche Signatur; `gfx` nutzt es per `using`), weil `core` nicht von `gfx` abhängen darf; dann `text_fold` implementieren. **Step 4:** GREEN (alle M1-Tests weiter grün).
- [ ] **Step 5:** Commit `feat(core): Titelbereinigung und Suchfaltung`.

---

### Task 3: Big-Endian-Formate `index.bin` und `meta.bin`

**Files:**
- Create: `src/core/bin_io.hpp/.cpp`, `src/core/index_file.hpp/.cpp`, `src/core/meta_db.hpp/.cpp`, `tools/metabin.py`, `tools/tests/test_metabin.py`
- Test: `tests/host/core/bin_io_test.cpp`, `index_file_test.cpp`, `meta_db_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl {
class BeWriter { public: void u8(u8); void u16(u16); void u32(u32); void u64(u64); void str(std::string_view);  // Länge > 65535 → gekürzt auf Codepoint-Grenze
                 const std::vector<u8>& bytes() const; };
class BeReader { public: explicit BeReader(std::span<const u8>);
                 bool u8(u8&); bool u16(u16&); bool u32(u32&); bool u64(u64&); bool str(std::string&);
                 bool ok() const; std::size_t remaining() const; };   // einmal false → bleibt false, liest nie über das Ende

inline constexpr u16 kIndexVersion = 1;
struct IndexFolder { std::string path; u32 mtime; };
struct IndexGame { Console console; u8 flags; std::string key, title, path; u64 size; };
enum IndexFlags : u8 { kFlagTitleFromHeader = 1, kFlagSecondDisc = 2, kFlagSplitFile = 4 };
struct IndexData { std::vector<IndexFolder> folders; std::vector<IndexGame> games; };
std::vector<u8> writeIndex(const IndexData&);
Result<IndexData> readIndex(std::span<const u8>);   // Magic "NLIX", Version 1; Konsole > 7 → Fehler

struct MetaEntry { std::string_view title; u16 year; u8 players; std::string_view description; };
class MetaDb {                                       // hält die Datei im Speicher, Einträge als Offsets
 public:
  static Result<MetaDb> parse(std::vector<u8> file); // Magic "NLMD", Version 1, Schlüssel aufsteigend sortiert (sonst Fehler)
  std::optional<MetaEntry> find(std::string_view key) const;   // binäre Suche, Bytevergleich
  u32 size() const;
};
std::string metaKey(const IndexGame& g);             // ID6 bzw. "<konsole>/<stem kleingeschrieben>" (Spec 5.2)
}
```

`tools/metabin.py`: `write_meta_bin(path: Path, entries: dict[str, tuple[str, int, int, str]]) -> None` (sortiert nach UTF-8-Bytes des Schlüssels) und `read_meta_bin(path) -> dict` für den Test.

- [ ] **Step 1: Tests schreiben.**

```cpp
TEST_CASE("writeIndex erzeugt exakt die erwarteten Bytes")   // 1 Ordner "roms/nes" mtime 0x01020304, 1 Spiel NES flags 0
     // key "nes/A.nes", title "A", path "roms/nes/A.nes", size 16 → feste Bytefolge im Test, beginnend 'N','L','I','X',0,1,0,0,0,0,0,1,0,0,0,1
TEST_CASE("readIndex(writeIndex(x)) == x mit Umlauten und 300 Spielen")
TEST_CASE("readIndex verwirft falsches Magic, falsche Version, abgeschnittene Datei, zu große Stringlänge, Konsole 9")
     // für jede Kürzung 0..n-1 der gültigen Datei: Ergebnis ist Fehler, kein Absturz
TEST_CASE("BeReader bleibt nach dem ersten Fehler im Fehlerzustand")
TEST_CASE("MetaDb findet Einträge per binärer Suche, unbekannter Schlüssel → nullopt")
TEST_CASE("MetaDb verwirft unsortierte Schlüssel und abgeschnittene Dateien")
TEST_CASE("metaKey") // Wii "RMCP01" → "RMCP01"; ROM "n64/Super Mario 64 (Europe).z64" → "n64/super mario 64 (europe)"
```
Python: `test_round_trip_and_sorting` (Schlüssel `"n64/b"`, `"RMCP01"`, `"n64/ä"` → Datei sortiert nach Bytes, `read_meta_bin` liefert alles zurück).

- [ ] **Step 2:** RED (Host und Tools). **Step 3:** Implementieren. **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(core): index.bin und meta.bin`.

---

### Task 4: Dateisystem-Interface und Disc-Header

**Files:**
- Create: `src/core/fs.hpp`, `src/core/disc_header.hpp/.cpp`, `tests/host/support/mem_fs.hpp`
- Test: `tests/host/core/disc_header_test.cpp`, `tests/host/core/mem_fs_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl {
struct DirEntry { std::string name; bool isDir; u64 size; u32 mtime; };
class FileSystem {            // Pfade mit Gerät: "sd:/roms/nes" bzw. im Test beliebig
 public:
  virtual ~FileSystem() = default;
  virtual Result<std::vector<DirEntry>> list(std::string_view dir) = 0;   // ohne "." und ".."
  virtual Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) = 0;   // liest bis zu out.size()
  virtual Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) = 0;
  virtual Result<void> writeAtomic(std::string_view file, std::span<const u8> data) = 0;   // tmp + rename
  virtual Result<void> makeDirs(std::string_view dir) = 0;
};
struct DiscInfo { char id6[7]; std::string title; };
std::optional<DiscInfo> parseWiiIso(std::span<const u8> first256);   // Magic 0x5D1C9EA3 @0x18
std::optional<DiscInfo> parseGcIso(std::span<const u8> first256);    // Magic 0xC2339F3D @0x1C
std::optional<DiscInfo> parseWbfs(std::span<const u8> first1024);    // "WBFS" @0, hd_sec_sz_s @8, Disc-Header @ (1 << hd_sec_sz_s)
inline constexpr std::size_t kDiscProbeBytes = 1024;                  // für alle drei genug, solange hd_sec_sz_s == 9
}
// tests/host/support/mem_fs.hpp
class MemFileSystem : public nl::FileSystem { public: void addFile(std::string path, std::vector<u8> data, u32 mtime = 0); void addDir(std::string path, u32 mtime = 0); /* … */ };
```

Titel aus dem Header: Bytes ab 0x20, bis zum ersten 0, max. 64 Byte; Nicht-ASCII-Bytes werden als Latin-1 nach UTF-8 übertragen (Header sind Shift-JIS/Latin-1, nicht UTF-8); ID6 nur aus `A–Z0–9`, sonst kein Treffer.

- [ ] **Step 1: Tests schreiben.**

```cpp
TEST_CASE("parseWiiIso liest ID6 und Titel")             // Puffer mit "RMCP01", Magic, Titel "Mario Kart Wii"
TEST_CASE("parseGcIso liest ID6 und Titel")
TEST_CASE("parseWbfs liest den Disc-Header am Sektor-Offset") // hd_sec_sz_s = 9 → Header bei 512
TEST_CASE("falsches Magic, ID6 mit Kleinbuchstaben/Steuerzeichen, zu kurzer Puffer, hd_sec_sz_s > 9 → nullopt")
TEST_CASE("Titel ohne 0-Terminator wird bei 64 Byte gekappt; Byte 0xE9 wird zu é")
TEST_CASE("MemFileSystem: list, read mit Offset hinter dem Ende (0 Bytes), writeAtomic, makeDirs")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(core): FileSystem-Interface und Disc-Header`.

---

### Task 5: Scanner

**Files:**
- Create: `src/core/scanner.hpp/.cpp`
- Modify: `src/core/console.hpp/.cpp` (Ordner und Endungen laut Spec 5.3)
- Test: `tests/host/core/scanner_test.cpp`

**Interfaces:**
- Consumes: `FileSystem`, `DirEntry`, `parseWiiIso/parseGcIso/parseWbfs`, `IndexData`, `MetaDb`, `metaKey`, `cleanTitle`, `compareTitles`.
- Produces:

```cpp
namespace nl {
// ConsoleInfo bekommt: const char* folder; std::array<const char*, 3> extensions;  (nullptr = unbenutzt)
// folder: "roms/nes" … "games" (GC), "wbfs" (Wii); extensions in Kleinbuchstaben mit Punkt.
struct ScanStats { u32 added = 0, removed = 0, headersRead = 0, skipped = 0; };
struct ScanResult { IndexData index; ScanStats stats; };
using ScanProgress = void (*)(void* user, u32 filesSeen);
Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot,      // "sd:" oder "usb:"
                               const IndexData* previous, const MetaDb* meta,
                               ScanProgress progress = nullptr, void* user = nullptr);
}
```

Regeln (Spec 6.2 plus Festlegungen oben):
- ROMs: `<root>/<folder>/` nicht rekursiv, Endung ohne Rücksicht auf Groß/klein; Schlüssel `<konsole>/<dateiname>`; Titel = Meta-Titel, sonst `cleanTitle(stem)`; auch 0-Byte-Dateien zählen (Fixtures).
- GC: `<root>/games/<ordner>/game.iso`; ohne gültigen Header → übersprungen (`skipped`); `disc2.iso` daneben → `kFlagSecondDisc`.
- Wii: `<root>/wbfs/<ordner>/*.wbfs|*.iso`; Header je nach Endung; `<stem>.wbf1` daneben → `kFlagSplitFile`; doppelte ID6 → erste Datei (nach Name) gewinnt, Rest `skipped`.
- Titel Wii/GC: Meta-Titel, sonst Header-Titel (`kFlagTitleFromHeader`), sonst ID6.
- Wiederverwendung: Spiel aus `previous` mit gleichem Pfad und gleicher Größe wird ohne Header-Lesen übernommen (Meta-Titel wird trotzdem neu angewandt).
- Fehlender Unterordner (z. B. kein `roms/gba`) ist kein Fehler; nicht lesbares `libRoot` → Fehler.
- `folders`: jeder gelistete Ordner mit mtime. Ergebnis sortiert nach Konsole (enum-Reihenfolge), dann `compareTitles`.
- `added`/`removed` = Schlüssel neu bzw. nicht mehr vorhanden gegenüber `previous`.

- [ ] **Step 1: Tests schreiben** (mit `MemFileSystem` und Header-Hilfsfunktionen im Test).

```cpp
TEST_CASE("ROMs: Endungen, Groß/klein, nicht rekursiv, readme.txt ignoriert")
TEST_CASE("ROM-Titel aus Dateiname bereinigt, Meta-Titel hat Vorrang")
TEST_CASE("GC: game.iso mit Header, disc2.iso setzt Flag, Ordner ohne game.iso wird ignoriert")
TEST_CASE("Wii: .wbfs und .iso, .wbf1 setzt Split-Flag, doppelte ID6 einmal")
TEST_CASE("kaputte Header werden übersprungen und gezählt, Scan läuft weiter")
TEST_CASE("abgeschnittene Datei (kürzer als 256 Byte) wird übersprungen")
TEST_CASE("bekannte Datei (Pfad+Größe) wird ohne Header-Lesen übernommen")   // headersRead == 0 beim zweiten Scan
TEST_CASE("geänderte Größe → Header wird neu gelesen")
TEST_CASE("added/removed gegenüber previous")
TEST_CASE("Sortierung: Konsole, dann Titel ohne Groß/klein")
TEST_CASE("fehlende Konsolenordner sind kein Fehler; nicht lesbares Wurzelverzeichnis ist einer")
TEST_CASE("Fortschritts-Callback wird mit steigender Anzahl aufgerufen")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(core): Bibliotheks-Scanner`.

---

### Task 6: Bibliotheks-Speicher, Favoriten, Verlauf

**Files:**
- Create: `src/core/library_store.hpp/.cpp`
- Modify: `src/core/game.hpp` (zusätzliche Felder), `src/ui/test_library.cpp` (kompiliert weiter)
- Test: `tests/host/core/library_store_test.cpp`

**Interfaces:**
- Consumes: `IndexData`, `MetaDb`, `metaKey`, `IniDoc`.
- Produces:

```cpp
namespace nl {
// Game (core/game.hpp) zusätzlich: const char* description = nullptr; const char* path = nullptr;
//                                    u64 size = 0; u8 flags = 0;
inline constexpr int kHistoryMax = 20;
class LibraryStore {
 public:
  void setGames(IndexData index, const MetaDb* meta);   // übernimmt Strings, baut Game-Sichten (stabil bis zum nächsten setGames)
  std::span<const Game> games() const;
  const Game* findByKey(std::string_view key) const;
  void loadFavorites(const IniDoc& ini);                 // [favorites], Zeilen ohne „=", Reihenfolge der Datei
  IniDoc favoritesIni() const;                           // unbekannte Schlüssel bleiben erhalten
  bool isFavorite(std::string_view key) const;
  bool toggleFavorite(std::string_view key);             // liefert neuen Zustand; neue Favoriten ans Ende
  void loadHistory(const IniDoc& ini);                   // [history] <unixzeit> = <schlüssel>, neueste zuerst, max 20
  u32 lastPlayed(std::string_view key) const;            // 0 = nie
  std::span<const Game* const> favorites() const;        // nur Schlüssel, die in games() vorkommen
  std::span<const Game* const> recent() const;
};
}
```

Meta-Felder: `year`, `players`, `description` aus `MetaDb` per `metaKey`; ohne Meta 0/nullptr.

- [ ] **Step 1: Tests schreiben.**

```cpp
TEST_CASE("setGames baut Game-Sichten mit Meta-Feldern; Zeiger bleiben bis zum nächsten setGames gültig")
TEST_CASE("Favoriten: Reihenfolge der Datei, unbekannte Schlüssel bleiben in favoritesIni(), erscheinen aber nicht in favorites()")
TEST_CASE("toggleFavorite hängt an und entfernt; Round-Trip über serialize/parse")
TEST_CASE("Schlüssel mit ; = [ ] Umlauten überstehen den Round-Trip") // "nes/Mario; Luigi = Bros [!] Ä.nes"
TEST_CASE("Verlauf: neueste zuerst, doppelte Schlüssel nur einmal (neuester Zeitpunkt), max 20, kaputte Zeitstempel ignoriert")
TEST_CASE("favorites() und recent() folgen setGames (Spiel entfernt → verschwindet, Favorit bleibt in der Datei)")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren (Strings in `std::deque<std::string>` oder einem Block, damit `const char*` stabil bleiben). **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(core): Bibliotheks-Speicher mit Favoriten und Verlauf`.

---

### Task 7: Fixture-Bibliothek und Deploy

**Files:**
- Create: `tools/make_fixtures.py`, `tools/tests/test_make_fixtures.py`, `tests/fixtures/sd/**` (erzeugt, committet)
- Modify: `tools/deploy_dolphin.py`, `tools/dolphin_smoke.py`, `tools/tests/test_dolphin.py`

**Interfaces (Produces):** `make_fixtures.main(out: Path) -> int`; `deploy(dol, sd_dir, dist_dir, fixtures_dir: Path | None = None)` kopiert zusätzlich die Fixtures; `dolphin_smoke.py --fixtures` (Standard an, `--no-fixtures` aus) deployt die Fixtures und löscht vorher `SDSync/newlauncher/cache/index.bin`.

Inhalt von `tests/fixtures/sd/` (12 gültige Spiele):

| Pfad | Inhalt |
|---|---|
| `newlauncher/config.ini` | `defaultConfigText()` mit `library = sd` |
| `newlauncher/favorites.ini` | `[favorites]` mit `RMCP01`, `nes/Super Mario Bros. (World).nes`, `n64/gibts nicht.z64` |
| `newlauncher/history.ini` | `[history]` mit `1762700000 = n64/Super Mario 64 (Europe) (En,Fr,De).z64` |
| `newlauncher/cache/meta.bin` | über `tools/metabin.py`: `RMCP01` („Mario Kart Wii", 2008, 4, Beschreibung), `n64/super mario 64 (europe) (en,fr,de)` („Super Mario 64", 1996, 1, Beschreibung) |
| `roms/nes/` | `Super Mario Bros. (World).nes`, `Zelda (Europe) (Rev 1).nes`, `1942 (Japan).nes` (0 Byte), `readme.txt` |
| `roms/snes/` | `Super Metroid (Europe) (En,Fr,De).sfc`, `F-Zero (Europe).SMC` |
| `roms/gb/` | `Tetris (World) (Rev 1).gb` |
| `roms/gbc/` | leer (`.keep`) |
| `roms/n64/` | `Super Mario 64 (Europe) (En,Fr,De).z64`, `Mario Kart 64 (Europe).z64` |
| `roms/gba/` | `Metroid Fusion (Europe).gba` |
| `games/Metroid Prime [GM8P01]/game.iso` | 1024 Byte, gültiger GC-Header |
| `wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs` | 1024 Byte, gültiger WBFS-Header |
| `wbfs/Super Mario Galaxy [RMGP01]/RMGP01.iso` | 1024 Byte, gültiger Wii-Header |
| `wbfs/Kaputt [XXXX01]/XXXX01.wbfs` | 1024 Byte Müll → übersprungen |

Die Dummy-Dateien enthalten nur Header, keine Spieldaten. `.gitignore` erlaubt `tests/fixtures/**` bereits.

- [ ] **Step 1: Tests schreiben** (Python): Fixture-Erzeugung in Temp-Ordner → erwartete Dateien vorhanden, Header-Magic an den richtigen Offsets, `meta.bin` liest sich mit `read_meta_bin`; Deploy kopiert Fixtures; Smoke mit Fake-Dolphin löscht altes `cache/index.bin`.
- [ ] **Step 2:** RED. **Step 3:** Implementieren, `python tools/make_fixtures.py tests/fixtures/sd` ausführen. **Step 4:** Tools-Tests GREEN.
- [ ] **Step 5:** Commit `test: Fixture-Bibliothek und Deploy mit Fixtures`.

---

### Task 8: Plattform – Mount, Dateisystem, Log-Datei, Startszene

**Files:**
- Create: `src/platform/storage.hpp/.cpp`, `src/platform/std_fs.hpp/.cpp`, `src/ui/startup_scene.hpp/.cpp`
- Modify: `src/platform/log.hpp/.cpp`, `CMakeLists.txt` (`fat` linken), `src/ui/strings.hpp`
- Test: `tests/host/ui/startup_scene_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl::platform {
bool mountSd();                                       // fatMountSimple("sd", &__io_wiisd)
class UsbMount {                                      // nicht blockierend, Versuch alle 500 ms
 public:
  enum class State : u8 { Waiting, Ready, TimedOut };
  void begin(int timeoutSec);
  State poll(float dtMs);                             // startup()+isInserted() → fatMountSimple("usb", &__io_usbstorage)
  int secondsLeft() const;
};
void unmountAll();
class StdFileSystem final : public FileSystem { /* opendir/readdir/stat, fopen/fread, writeAtomic = tmp + remove + rename */ };
}
namespace nl::log { void flushLogFile(FileSystem& fs, std::string_view path); }   // Ringpuffer 16 KB → Datei
namespace nl::ui {
class StartupScene final : public Scene {
 public:
  enum class Choice : u8 { None, Retry, UseSd };
  void showWaitingUsb(int secondsLeft);               // „Warte auf USB … (n s)"
  void showUsbFailed();                               // Dialog „Kein USB-Gerät gefunden" + Knöpfe „Erneut versuchen" / „SD verwenden"
  void showScanning(u32 filesSeen);                   // „Bibliothek wird durchsucht … n Dateien"
  Choice takeChoice();                                // einmalig, danach None
};
}
```

- [ ] **Step 1: Host-Tests für `StartupScene`:** Dialog nur im Zustand „UsbFailed" bedienbar; Left/Right wechseln zwischen den zwei Knöpfen; Confirm liefert Retry bzw. UseSd genau einmal; Zeiger auf Knopf + Confirm; in den anderen Zuständen kein Choice; draw ohne verworfene Quads in 640 und 854.
- [ ] **Step 2:** RED. **Step 3:** Implementieren (Host-Teil), dann Wii-Teil. **Step 4:** Host-Tests GREEN, Wii-Build ohne Warnungen.
- [ ] **Step 5:** Commit `feat(platform): Mount, Dateisystem, Log-Datei, Startszene`.

---

### Task 9: Startablauf, Scan-Thread, Bibliothek in der UI

**Files:**
- Create: `src/platform/scan_worker.hpp/.cpp`
- Modify: `src/app/main.cpp`, `src/ui/main_scene.hpp/.cpp`, `src/ui/layout.hpp`, `src/ui/strings.hpp`
- Test: `tests/host/ui/main_scene_test.cpp` (erweitert)

**Interfaces:**
- Consumes: Task 1–8.
- Produces:

```cpp
namespace nl::platform {
class ScanWorker {                                   // ein LWP-Thread (Stack 64 KB, Priorität 40), ein Mutex
 public:
  bool start(FileSystem& fs, std::string libRoot, const IndexData* previous, const MetaDb* meta);  // false wenn schon aktiv
  u32 filesSeen() const;                             // für die Fortschrittsanzeige
  std::optional<Result<ScanResult>> take();          // fertiges Ergebnis genau einmal, sonst nullopt
};
}
namespace nl::ui {
// MainScene zusätzlich:
void MainScene::setLibrary(const Library& lib);     // Leiste neu; Eintrag (Art + Konsole) und Fokus (Schlüssel) bleiben, wenn möglich
}
```

Startablauf in `main` (Spec 6.1):
1. `mountSd()`; `sd:/newlauncher/config.ini` lesen, sonst Ordner anlegen und `defaultConfigText()` schreiben → `NL_CONFIG ok|default`. Ohne SD: Standardwerte.
2. Video, Renderer, Atlas wie M1; Theme aus `Config` (Modus, dunkel von/bis).
3. Bibliotheksgerät: `Sd` → `"sd:"`; `Usb` → `StartupScene` mit `UsbMount` bis Ready oder TimedOut, dann Dialog; „SD verwenden" gilt nur für diese Sitzung → `NL_MOUNT <lib>`.
4. `cache/index.bin` und `cache/meta.bin` lesen. Index gültig → `LibraryStore::setGames`, UI sofort (`NL_LIBRARY … source=index`), Hintergrund-Scan starten. Kein/kaputter Index → Scan mit Fortschritt in der `StartupScene`, danach UI (`source=scan`).
5. Favoriten und Verlauf von `sd:/newlauncher/` laden.
6. Pro Frame `ScanWorker::take()`: Ergebnis → `setGames`, `MainScene::setLibrary`, `index.bin` per `writeAtomic` → `NL_INDEX_SAVED`, `NL_SCAN_DONE …`. Scan-Fehler → Log, alte Liste bleibt.
7. Nach `NL_READY` und beim Beenden `flushLogFile(fs, "sd:/newlauncher/log.txt")`.

- [ ] **Step 1: Tests für `setLibrary`:**

```cpp
TEST_CASE("setLibrary behält Leisten-Eintrag und fokussiertes Spiel per Schlüssel")
TEST_CASE("fokussiertes Spiel entfernt → Fokus 0 bzw. -1 bei leerer Liste")
TEST_CASE("Konsole verschwindet → Leiste fällt auf den Eintrag an gleicher Position bzw. den letzten")
TEST_CASE("leere Bibliothek → nur Favoriten/Zuletzt, kein Absturz")
```
(Keine Allokation im Leerlauf nach einem Bibliothekswechsel prüft der Selbsttest über `NL_ALLOC_IDLE 0`.)

- [ ] **Step 2:** RED. **Step 3:** Implementieren (`setLibrary` → `scratch_` neu dimensionieren, Leiste neu bauen). Danach `ScanWorker` und `main`.
- [ ] **Step 4:** Host-Tests GREEN; Wii-Build; `python tools/dolphin_smoke.py --marker NL_READY --marker "NL_LIBRARY games=12" --marker "NL_SCAN_DONE games=12"`. Expected: Exit 0. Screenshot: Leiste zeigt Favoriten, Zuletzt, Wii, GC, GBA, N64, SNES, GB, NES (kein GBC).
- [ ] **Step 5:** Commit `feat: Bibliothek beim Start laden, Hintergrund-Scan, Index speichern`.

---

### Task 10: Favorit-Taste, Hinweise, Favoriten-Markierung

**Files:**
- Modify: `src/ui/scene.hpp` (`SceneRequests` + `AppCommand::ToggleFavorite`), `src/ui/main_scene.hpp/.cpp`, `src/ui/strings.hpp`, `src/app/main.cpp`
- Test: `tests/host/ui/main_scene_test.cpp`, `tests/host/ui/scene_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl::ui {
enum class AppCommand : u8 { None, ExitToHbc, WiiMenu, PowerOff, CycleTheme, ToggleFavorite };   // appCommandName: "toggle_favorite"
struct SceneRequests { Scene* push = nullptr; bool pop = false; AppCommand command = AppCommand::None; const Game* game = nullptr; };
// MainScene zusätzlich:
void MainScene::showToast(const char* text);        // ersetzt die Infozeile für 2 s (wie „Darstellung: …")
void MainScene::setFavoriteKeys(const LibraryStore* store);   // für die Markierung auf den Kacheln (isFavorite)
}
```

Verhalten: `Action::Favorite` auf fokussierter Kachel → `command = ToggleFavorite`, `game = fokussiertes Spiel`. `main`: `toggleFavorite`, `favorites.ini` per `writeAtomic` (`NL_FAV_SAVED`), `setLibrary` (Favoriten-Liste neu), Hinweis „Zu Favoriten hinzugefügt" bzw. „Aus Favoriten entfernt". Favoriten-Kacheln tragen oben rechts einen Akzentpunkt (Kreis Ø 8). Ohne Fokus: keine Wirkung.

- [ ] **Step 1: Tests:** Favorit-Taste mit Fokus liefert Kommando und Spiel; ohne Fokus nichts; `showToast` ersetzt die Infozeile 2 s; Favoriten-Kachel zeichnet einen Quad mehr als eine normale; `appCommandName(ToggleFavorite) == "toggle_favorite"`.
- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN; Wii-Build.
- [ ] **Step 5:** Commit `feat(ui): Favoriten umschalten und speichern`.

---

### Task 11: A–Z-Sprungleiste

**Files:**
- Create: `src/ui/jump_bar.hpp/.cpp`
- Modify: `src/core/text_fold.hpp/.cpp` (`firstIndexForBucket`), `src/ui/main_scene.hpp/.cpp`
- Test: `tests/host/core/text_fold_test.cpp`, `tests/host/ui/jump_bar_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl {
// Erster Index mit genau diesem Buchstaben; sonst der mit dem kleinsten folgenden Buchstaben
// ('#' < 'A' < … < 'Z'); sonst der letzte Index; leere Liste → -1. Funktioniert auch unsortiert.
int firstIndexForBucket(std::span<const Game* const> games, char bucket);
}
namespace nl::ui {
class JumpTarget { public: virtual ~JumpTarget() = default; virtual void jumpToBucket(char b) = 0; virtual void openSearch() = 0;
                   virtual bool hasBucket(char b) const = 0; };
class JumpBar final : public Scene {                 // Overlay: 28 Felder "#", "A"–"Z", "Suche" in einer Reihe über der Infoleiste
 public:
  explicit JumpBar(JumpTarget& target);
  int focus() const;                                  // 0 = "#", 1–26 = A–Z, 27 = Suche
};
}
```

`MainScene` implementiert `JumpTarget`; `Action::Search` öffnet die Leiste (`push`), `Confirm` springt bzw. öffnet die Suche und schließt die Leiste (`pop`); Back/Search/Home schließen. Buchstaben ohne Titel sind gedimmt (`textMuted`), aber wählbar. `onEnter` setzt den Fokus auf den Buchstaben des aktuell fokussierten Spiels.

- [ ] **Step 1: Tests:**

```cpp
TEST_CASE("firstIndexForBucket") // Titel ["1080", "Banjo", "Donkey"]: '#'→0, 'B'→1, 'C'→2, 'Z'→2, leer→-1; unsortiert ["Zelda","Arms"]: 'A'→1, 'B'→0
TEST_CASE("JumpBar: Left/Right ohne Umlauf, Confirm springt und schließt, Feld 27 öffnet Suche")
TEST_CASE("JumpBar: Zeiger auf Feld + Confirm; Zeiger neben der Leiste blockiert Confirm")
TEST_CASE("JumpBar: onEnter fokussiert den Buchstaben des aktuellen Spiels")
TEST_CASE("MainScene: jumpToBucket fokussiert das erste passende Spiel und scrollt hin")
TEST_CASE("draw in 640 und 854: alle 28 Felder im Bild, keine verworfenen Quads")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(ui): A–Z-Sprungleiste`.

---

### Task 12: Suche mit Bildschirmtastatur

**Files:**
- Create: `src/ui/keyboard.hpp/.cpp`
- Modify: `src/ui/main_scene.hpp/.cpp`, `src/ui/layout.hpp/.cpp` (`BarKind::Search`), `src/ui/strings.hpp`
- Test: `tests/host/ui/keyboard_test.cpp`, `tests/host/ui/main_scene_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl::ui {
class SearchTarget { public: virtual ~SearchTarget() = default; virtual void setQuery(std::string_view q) = 0; };
class Keyboard final : public Scene {                // Overlay, untere Bildschirmhälfte
 public:
  explicit Keyboard(SearchTarget& target);
  std::string_view text() const;                    // max. 32 Byte
};
// Tastenreihen (QWERTZ): "1234567890" / "QWERTZUIOP" / "ASDFGHJKL" / "YXCVBNM" / [Leerzeichen][Löschen][Fertig]
// MainScene: setQuery(q) ≠ leer → Such-Eintrag „Suche: q" ganz vorn in der Leiste, aktiv, Treffer aus allen Konsolen
//            (foldForSearch(title) mit matchesQuery), sortiert nach compareTitles; leere Anfrage → Suchmodus aus.
//            Back im Hauptbildschirm bei aktivem Suchmodus → Suchmodus aus, vorheriger Eintrag wieder aktiv.
}
```

Bedienung: Steuerkreuz zwischen Tasten (oben/unten: Taste mit der nächstgelegenen Mitte), Zeiger wie gehabt, Confirm tippt; jede Änderung ruft sofort `setQuery` (Live-Filter). „Fertig" oder Back schließt die Tastatur, der Suchmodus bleibt. Leere Trefferliste → „Keine Treffer".

- [ ] **Step 1: Tests:**

```cpp
TEST_CASE("Keyboard: Tippen, Löschen, Leerzeichen; max. 32 Byte; jede Änderung ruft setQuery")
TEST_CASE("Keyboard: Hoch/Runter wählt die Taste mit der nächsten Mitte; Ränder klemmen")
TEST_CASE("Keyboard: Fertig und Back schließen (pop), Text bleibt")
TEST_CASE("MainScene: setQuery(\"mario\") zeigt Treffer aus allen Konsolen, Such-Eintrag aktiv, Fokus 0")
TEST_CASE("MainScene: Suche ignoriert Groß/klein, Akzente und No-Intro-Tags") // "pokemon" findet "Pokémon Snap"; "europe" findet nichts
TEST_CASE("MainScene: keine Treffer → kein Fokus, „Keine Treffer\"")
TEST_CASE("MainScene: Back beendet den Suchmodus und stellt den vorherigen Eintrag wieder her")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN.
- [ ] **Step 5:** Commit `feat(ui): Suche mit Bildschirmtastatur`.

---

### Task 13: Detailansicht

**Files:**
- Create: `src/ui/details_scene.hpp/.cpp`
- Modify: `src/ui/main_scene.cpp` (Confirm auf Kachel öffnet Details), `src/app/main.cpp`, `src/ui/strings.hpp`
- Test: `tests/host/ui/details_scene_test.cpp`

**Interfaces (Produces):**

```cpp
namespace nl::ui {
class DetailsScene final : public Scene {            // Vollbild-Szene (kein Overlay)
 public:
  void open(const Game& game, bool favorite, u32 lastPlayedUnix);
  void setFavorite(bool favorite);
  int focusedButton() const;                         // 0 Starten, 1 Favorit
};
int detailsInfoLine(const Game& g, u32 lastPlayedUnix, char* out, std::size_t cap);
    // "N64 · Gen 5 · 1996 · 1 Spieler · zuletzt 09.11.2025"; fehlende Teile entfallen
}
```

Aufbau (Spec 3.2): links Platzhalter-Cover 200×280 (logisch, auf die verfügbare Höhe eingepasst) mit Konsolenkürzel; rechts Titel (`Title`, max. 2 Zeilen), Infozeile, Beschreibung (`Body`, max. 3 Zeilen mit Ellipse), darunter Knöpfe „Starten" (Akzent) und „Favorit" bzw. „Favorit entfernen". Infoleiste unten: „A Starten · B Zurück · 1 Favorit" und Uhrzeit. „Starten" zeigt in M2 den Hinweis „Starten folgt mit M3". Favorit-Taste oder Knopf → `ToggleFavorite` wie in Task 10. Back → `pop`.

`MainScene::setDetails(DetailsScene* details, const LibraryStore* store)`: Confirm auf fokussierter Kachel (Tasten oder Zeiger auf der Kachel) ruft `details->open(game, store->isFavorite(key), store->lastPlayed(key))` und fordert `push` an. `main` meldet Favoriten-Änderungen per `setFavorite` an die offene Detailansicht zurück.

- [ ] **Step 1: Tests:**

```cpp
TEST_CASE("detailsInfoLine mit allen Feldern / ohne Jahr / ohne Verlauf")   // Datum aus Unixzeit, UTC
TEST_CASE("Details: Left/Right zwischen Starten und Favorit, Confirm auf Favorit → ToggleFavorite mit Spiel")
TEST_CASE("Details: Starten zeigt Hinweis, kein Kommando")
TEST_CASE("Details: Back schließt")
TEST_CASE("Details: Spiel ohne Beschreibung und mit 500 Zeichen Beschreibung – max. 3 Zeilen, keine verworfenen Quads")
TEST_CASE("MainScene: Confirm auf fokussierter Kachel öffnet Details; ohne Fokus nicht")
```

- [ ] **Step 2:** RED. **Step 3:** Implementieren. **Step 4:** GREEN; Wii-Build; Screenshot der Detailansicht in Dolphin.
- [ ] **Step 5:** Commit `feat(ui): Detailansicht`.

---

### Task 14: Selbsttest, CI, README, Abnahme

**Files:**
- Modify: `src/app/selftest.cpp`, `.github/workflows/build.yml`, `README.md`

Selbsttest-Skript (Fixture-Bibliothek, 12 Spiele) erweitert um:
1. Warten auf die Bibliothek: `NL_LIBRARY games=12`, Leiste hat 9 Einträge.
2. Leiste auf „N64": `focusIndex` 0, Favorit-Taste → `NL_FAV_SAVED`, Favoriten-Liste hat danach 3 Einträge.
3. Taste 2 → A–Z-Leiste, Buchstabe „S" → fokussiertes Spiel beginnt mit „S" (Super Mario 64).
4. A–Z → „Suche", Tastatur „MARIO" tippen → 5 Treffer (Super Mario Bros., Super Mario 64, Mario Kart 64, Mario Kart Wii, Super Mario Galaxy); Back, Back → vorheriger Eintrag.
5. Confirm → Detailansicht, `depth() == 2`; Back → 1.
6. Leerlauf-Allokationen 0, `NL_SELFTEST_OK`, Absturz wie in M1.

- [ ] **Step 1:** Skript erweitern. **Step 2:** `python tools/dolphin_smoke.py --dol build/wii/nl_selftest.dol --marker "NL_LIBRARY games=12" --marker NL_FAV_SAVED --marker "NL_ALLOC_IDLE 0" --marker NL_SELFTEST_OK --marker "NL_CRASH id=7"` → Exit 0.
- [ ] **Step 3:** CI: keine Änderung am Ablauf nötig, Fixtures liegen im Repo; prüfen, dass `tools/tests` die neuen Python-Tests mitnimmt. README: Dateien auf der SD (Spec 5.1), Fixture-Befehl, neue Marker, Bedienung von A–Z, Suche und Details.
- [ ] **Step 4:** Gesamtprüfung: Host, Tools, Wii-Build, Smoke (mit Fixtures), Selbsttest, Screenshots (Hauptbildschirm mit echter Bibliothek, Suche, A–Z, Details) in hell und dunkel.
- [ ] **Step 5:** Commit `feat: Selbsttest für die Bibliothek, README, Abnahme M2`.

---

## Abnahme M2 (Host + Dolphin)

- [ ] Fixture-Bibliothek erscheint vollständig (12 Spiele, kaputte Datei übersprungen), richtige Titel (Meta vor Header vor Dateiname).
- [ ] Zweiter Start lädt aus `index.bin` (`source=index`); Hintergrund-Scan erkennt eine neu kopierte ROM ohne Neustart der UI.
- [ ] Favoriten umschalten bleibt nach Neustart erhalten; „Zuletzt" zeigt den Verlauf aus `history.ini`.
- [ ] A–Z-Sprung, Suche (Live-Filter, Akzente/Tags ignoriert) und Detailansicht funktionieren mit Tasten und Zeiger.
- [ ] USB-Warten und Dialog „Erneut versuchen / SD verwenden" sind host-getestet; echte USB-Prüfung folgt in M7.
- [ ] Leerlauf ohne Allokationen, Host-/Tools-Tests und CI grün.

## Bewusst nicht in M2

Spiele starten und Verlauf schreiben, `emulators.ini`, Chainload-Test mit `argv_echo` (M3); N64-Startmethode, `n64_vc.ini`, `games.ini` (M4); Cover, `.tex` und `prepare_meta.py` (M5); Einstellungen, Sounds, Musik, DE/EN (M6); Messung auf echter Wii und USB (M7). Die `StartupScene` dient in M2 als Splash (Spec 6.1 Schritt 3); das Ziel „< 0,5 s" wird in M7 gemessen.
