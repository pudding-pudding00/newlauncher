# M3 – Emulator-Start: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** „Starten“ in der Detailansicht startet NES-, SNES-, GB-, GBC- und GBA-Spiele über den Emulator aus `emulators.ini`. Dazu lädt ein eigener Chainloader samt App-Booter die Emulator-DOL und übergibt `argv`. Kann ein Spiel nicht starten, erklärt ein Preflight-Dialog den Grund, und der Launcher läuft weiter.

**Architecture:** Alles Prüfbare liegt plattformfrei in `core/`:
- DOL-Header lesen und prüfen
- `emulators.ini` mit Standardwerten
- Argument-Platzhalter auflösen
- `argv`-Block bauen
- Preflight über das `FileSystem`-Interface
- Verlauf schreiben

Auf der Wii-Seite kommen dazu:
- `launch/chainloader.cpp`: lädt die DOL in einen reservierten MEM2-Bereich, fährt das System herunter und springt in den App-Booter.
- Der App-Booter: ein eigenes, freistehendes C-Programm, das als Binärdatei eingebettet ist.

Die Test-DOL `argv_echo` gibt ihre Argumente ins Log aus. Ein Autostart-Schalter in `config.ini` macht den ganzen Weg in Dolphin automatisch prüfbar.

**Tech Stack:** wie M2. Dazu kommt ein zweites devkitPPC-Ziel ohne libogc-Startcode (App-Booter, Linkerskript, `objcopy -O binary`). Eingebettet wird es über `dkp_add_embedded_binary_library`.

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md` (§3.2, §4.1, §4.2, §5.1, §5.4 `emulators.ini`, §6.5, §8.2 Chainload-Test, §8.3, §8.5, §9 M3, §10)

**Basis:** Branch `feat/m3-emulator-start` zweigt von `feat/m2-bibliothek` ab, solange PR #4 noch nicht gemergt ist. Nach dem Merge wird auf `main` rebased.

## Global Constraints

- Alle Global Constraints aus M1 und M2 gelten weiter: C++20 ohne Exceptions und RTTI, `Result`, keine Allokation im Leerlauf (`NL_ALLOC_IDLE 0`), Dolphin nur mit `-u`, keine ROMs/ISOs/WADs außer Dummy-Fixtures, `core/` ohne `<ogc/...>`.
- `emulators.ini` liegt unter `sd:/newlauncher/emulators.ini`. `args` ist eine durch `|` getrennte Liste; jeder Eintrag wird ein `argv`-Element. `argv[0]` ist immer der DOL-Pfad. Platzhalter sind `{dir}` (Ordner mit abschließendem `/`), `{file}` (Dateiname) und `{path}` (vollständiger Pfad).
- Standardwerte laut Spec §5.4:
  - `nes` = `sd:/apps/fceugx/boot.dol`
  - `snes` = `sd:/apps/snes9xgx/boot.dol`
  - `gb`, `gbc` und `gba` = `sd:/apps/vbagx/boot.dol`
  - `args = {dir}|{file}` für alle fünf
  - Die Abschnitte `n64_not64`, `n64_wii64` und `nintendont` werden nur gelesen und erhalten, aber erst ab M4/M8 benutzt.
- Ablauf beim Start (Spec §6.5): `preflight()` → bei Fehler Dialog, der Launcher bleibt. Sonst: Verlauf speichern → (Audio stoppen, ab M6) → Video herunterfahren → `launch()`. Der Preflight prüft alles und ändert nichts.
- Spielpfad = `<lib>:/` + `Game.path`, z. B. `sd:/roms/nes/Zelda (Europe) (Rev 1).nes`.
- Speicher-Layout des Chainloaders, festgelegt in `core/launch_layout.hpp`:
  - `kDolBufferAddr = 0x92800000`
  - `kBooterAddr = 0x93300000`
  - `kArgsAddr = 0x93300800`
  - `kReservedEnd = 0x933E0000`
  - `kMaxDolBytes = kBooterAddr - kDolBufferAddr` (11 MiB)
  - `main` setzt als erste Anweisung `SYS_SetArena2Hi((void*)kDolBufferAddr)`, damit der Heap diesen Bereich nie belegt.
- Neue Log-Marker:
  - `NL_EMULATORS <ok|default>`
  - `NL_PREFLIGHT_FAIL <code> <schlüssel>`
  - `NL_LAUNCH <schlüssel>`
  - `NL_HISTORY_SAVED`
  - `NL_AUTOSTART <schlüssel>`
  - aus `argv_echo`: `NL_ARGV argc=<n>` und `NL_ARGV <i> <wert>`
- Commits enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Emulator-DOL fehlt, ist leer, abgeschnitten oder keine DOL**, z. B. eine `boot.elf` unter falschem Namen oder 0 Byte. Erwartet: Preflight-Dialog mit Pfad, kein Sprung ins Leere (Tests in Task 1 und 3).
2. **Sonderzeichen und Länge im ROM-Pfad** (Leerzeichen, Klammern, `,`, `&`, Umlaute, sehr lange Namen). Erwartet: `argv` enthält die Bytes unverändert; ein zu großer `argv`-Block wird im Preflight abgelehnt statt den Booter zu überschreiben (Tests in Task 2).
3. **Kaputte oder unvollständige `emulators.ini`** (Abschnitt fehlt, `dol` leer, `args` leer oder mit unbekanntem Platzhalter). Erwartet: Standardwert für fehlende Schlüssel; unbekannte Platzhalter bleiben wörtlich stehen; leeres `args` ergibt nur `argv[0]` (Tests in Task 2).
4. **Konsole ohne M3-Backend** (N64, GC, Wii). Erwartet: Dialog „Starten für <Konsole> folgt in einer späteren Version“, kein Absturz (Tests in Task 3 und 5).
5. **DOL-Sektionen, die den reservierten Bereich oder den Booter überschreiben würden.** Erwartet: Der Preflight lehnt die DOL als ungültig ab (Test in Task 1).

## Abweichungen und Festlegungen gegenüber der Spec

| Punkt | Festlegung in M3 | Grund |
|---|---|---|
| Starten aus dem Raster | nur über die Detailansicht (A öffnet Details, A auf „Starten“ startet) | Spec §3.2: A öffnet die Details; ein Schnellstart steht nirgends |
| App-Booter | eigener Code (C, ca. 100 Zeilen), kein GPL-Booter | Spec §6.5 beschreibt nur den Mechanismus; `boot/` mit GPL-Code ist M9 |
| `argv`-Block | libogc-Format: Magic `0x5f617267`, Kommandozeile = Argumente je mit `\0` abgeschlossen, plus ein abschließendes `\0`; Länge ohne das letzte `\0`. Der Booter schreibt den Block nur, wenn an `entry + 4` die Kennung `_arg` steht | so liest libogc-`crt0` (`__CheckARGV`) die Argumente; alle GX-Emulatoren sind libogc-Programme |
| Autostart | `[debug] autostart = <schlüssel>` in `config.ini`; das Smoke-Skript setzt die Zeile nur mit `--autostart` in die deployte Konfiguration, die Fixture-Datei bleibt ohne | sonst würde jeder normale Smoke- oder Selbsttest nach `NL_READY` wegspringen |
| Audio stoppen | entfällt bis M6 | es gibt noch kein Audio |
| N64 und GC in `emulators.ini` | werden gelesen und beim Schreiben erhalten, aber nicht gestartet | M4/M8 |

## Dateistruktur

```
src/core/
  dol.hpp/.cpp              DolHeader, parseDol (Prüfung gegen Layout)
  launch_layout.hpp         Adressen des Chainloaders (constexpr)
  emulators.hpp/.cpp        EmulatorTable aus emulators.ini + Standardwerte
  launch_args.hpp/.cpp      expandArgs, packArgvBlock
  launch_plan.hpp/.cpp      LaunchPlan, preflightEmulator
  library_store.*           + recordPlayed, historyIni
  config.*                  + debugAutostart
src/ui/
  dialog_scene.hpp/.cpp     Overlay mit Titel, Text, Knopf „OK“
  launch_text.hpp/.cpp      Fehler → Dialogtext
  details_scene.*           „Starten“ → AppCommand::Launch
  scene.*                   + AppCommand::Launch
src/launch/
  chainloader.hpp/.cpp      chainload(dolBytes, argvBlock) (Wii)
  app_booter/booter.c       freistehender Booter
  app_booter/booter.ld      Linkerskript, Basis 0x93300000
tests/wii/argv_echo.c       Test-DOL
tools/                      make_fixtures, deploy_dolphin, dolphin_smoke erweitert
```

---

### Task 1: DOL-Header und Speicher-Layout

**Files:**
- Create: `src/core/dol.hpp`, `src/core/dol.cpp`, `src/core/launch_layout.hpp`, `tests/host/core/dol_test.cpp`, `tests/host/support/dol_images.hpp`
- Modify: `CMakeLists.txt` (`src/core/dol.cpp` in `nl_core`)

**Interfaces:**
- Produces:
  - `struct DolSection { u32 offset, address, size; };`
  - `struct DolHeader { std::array<DolSection, 18> sections; u32 bssAddress, bssSize, entry; };` (0–6 Text, 7–17 Daten)
  - `Result<DolHeader> parseDol(std::span<const u8> head, u64 fileSize);` mit `head` ≥ `kDolHeaderBytes = 0x100`
  - `launch_layout.hpp`: Konstanten aus den Global Constraints plus `kMem1Begin = 0x80003F00`, `kMem1End = 0x81800000`, `kMem2Begin = 0x90000000`
  - Test-Helfer `test::dolImage(std::vector<DolSection> text, std::vector<DolSection> data, u32 entry)` (Big-Endian-Header, Sektionsdaten mit Füllbytes)

- [ ] **Step 1: Failing Tests schreiben** (`dol_test.cpp`)
  - „parseDol liest Sektionen, BSS und Einsprung“: ein Image mit einer Text-Sektion `{0x100, 0x80004000, 0x200}`, Einsprung `0x80004000` → `isOk`, `sections[0].address == 0x80004000`, `entry == 0x80004000`
  - „Datei kürzer als Header oder Sektion endet hinter Dateiende → InvalidData“: Größe 0x80; Sektion `offset + size > fileSize`
  - „keine Text-Sektion oder Einsprung außerhalb aller Text-Sektionen → InvalidData“
  - „Ziel außerhalb MEM1/MEM2 oder überlappt den reservierten Bereich → InvalidData“: Adresse `0x70000000`; Adresse `0x92900000` (im DOL-Puffer); Adresse `0x933000F0` (Booter)
  - „leere Sektionen (size 0) werden ignoriert, egal welche Adresse“
- [ ] **Step 2: Ausführen.** Erwartet: Kompilierfehler, `core/dol.hpp` fehlt.
- [ ] **Step 3: Umsetzen.** Feld-Layout: 7 Text- und 11 Daten-Offsets ab 0x00, Adressen ab 0x48, Größen ab 0x90, BSS bei 0xD8/0xDC, Einsprung bei 0xE0, alles Big-Endian über `BeReader`. Ein Ziel ist gültig, wenn `[address, address+size)` ganz in `[kMem1Begin, kMem1End)` oder ganz in `[kMem2Begin, kDolBufferAddr)` liegt. Die Fehler-Kontexte nennen die Sektion („Sektion 3 außerhalb des Speichers“).
- [ ] **Step 4: Ausführen.** Erwartet: alle Host-Tests grün.
- [ ] **Step 5: Commit** `feat(core): DOL-Header prüfen und Chainloader-Speicherlayout`

### Task 2: `emulators.ini`, Argumente und `argv`-Block

**Files:**
- Create: `src/core/emulators.hpp/.cpp`, `src/core/launch_args.hpp/.cpp`, `tests/host/core/emulators_test.cpp`, `tests/host/core/launch_args_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `IniDoc` (M2), `Console`, `consoleInfo` (M2)
- Produces:
  - `struct EmulatorEntry { std::string dol; std::string args; };`
  - `class EmulatorTable { public: static EmulatorTable fromIni(const IniDoc&); const EmulatorEntry* forConsole(Console) const; /* nullptr für N64/GC/Wii */ };`
  - `std::string defaultEmulatorsText();` (Text exakt wie Spec §5.4)
  - `std::vector<std::string> expandArgs(std::string_view dolPath, std::string_view schema, std::string_view romPath);`
  - `Result<std::vector<u8>> packArgvBlock(std::span<const std::string> args, u32 baseAddr);` mit `kMaxArgvBytes = 0x800`

- [ ] **Step 1: Failing Tests schreiben**
  - `emulators_test`:
    - „Standardwerte wie Spec 5.4“: `fromIni(IniDoc::parse(""))` → `forConsole(Console::Gba)->dol == "sd:/apps/vbagx/boot.dol"` und `args == "{dir}|{file}"`
    - „Datei überschreibt nur vorhandene Schlüssel“: `[nes]\ndol = sd:/apps/argv_echo/boot.dol` → nes-`dol` neu, nes-`args` Standard, snes unverändert
    - „N64, GC und Wii liefern nullptr“
    - „defaultEmulatorsText ergibt nach dem Parsen dieselben Werte und enthält [n64_not64], [n64_wii64], [nintendont]“
  - `launch_args_test`:
    - „{dir}|{file}“ mit `sd:/roms/nes/Zelda (Europe) (Rev 1).nes` → `{"sd:/apps/fceugx/boot.dol", "sd:/roms/nes/", "Zelda (Europe) (Rev 1).nes"}`
    - „{path}“ → zwei Elemente, das zweite ist der volle Pfad
    - „leeres Schema → nur argv[0]“
    - „unbekannter Platzhalter {foo} und Text um Platzhalter bleiben wörtlich“: `--rom={file}` → `--rom=Zelda (Europe) (Rev 1).nes`; `{foo}` bleibt `{foo}`
    - „packArgvBlock: feste Bytes“: Argumente `{"a:/x", "y"}`, Basis `0x93300800` → die ersten 24 Byte sind der Header, Big-Endian:
      - Magic `0x5f617267`
      - `commandLine = 0x93300818`
      - `length = 7` (die Kommandozeile `a:/x\0y\0\0` ohne das letzte `\0`)
      - `argc = 0`, `argv = 0`
      - `endARGV = 0`
      - Danach folgen genau diese Bytes.
    - „packArgvBlock: Block größer als 0x800 → InvalidData“
    - „Bytes ≥ 0x80 (UTF-8-Umlaut) gehen unverändert durch“
- [ ] **Step 2: Ausführen.** Erwartet: Kompilierfehler.
- [ ] **Step 3: Umsetzen.**
  - Konsole → Abschnitt über `consoleInfo(c).key`; nur `nes`, `snes`, `gb`, `gbc` und `gba` liefern einen Eintrag.
  - `{dir}` = Pfad bis einschließlich zum letzten `/`; `{file}` = der Rest.
  - Beim Ersetzen wird jedes Vorkommen ersetzt.
- [ ] **Step 4: Ausführen.** Erwartet: grün.
- [ ] **Step 5: Commit** `feat(core): emulators.ini, Argument-Schema und argv-Block`

### Task 3: Preflight und Verlauf

**Files:**
- Create: `src/core/launch_plan.hpp/.cpp`, `tests/host/core/launch_plan_test.cpp`
- Modify: `src/core/library_store.hpp/.cpp`, `tests/host/core/library_store_test.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `parseDol` (Task 1), `EmulatorTable`, `expandArgs`, `packArgvBlock` (Task 2), `FileSystem` (M2)
- Produces:
  - `struct LaunchPlan { std::string dolPath; u32 dolSize; std::vector<std::string> args; std::vector<u8> argvBlock; };`
  - `Result<LaunchPlan> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus);`
  - Fehlercodes:
    - `Unsupported`: Konsole ohne Backend in M3; Kontext = Konsolenname
    - `NotFound`: DOL fehlt; Kontext = DOL-Pfad
    - `InvalidData`: DOL ungültig oder zu groß (`> kMaxDolBytes`); Kontext = DOL-Pfad
    - `NotFound` mit Kontext = ROM-Pfad: ROM fehlt
    - `InvalidData`: `argv` zu lang
  - In `LibraryStore`:
    - `void recordPlayed(std::string_view key, u32 unixTime);` (an den Anfang, Duplikat entfernen, max. `kHistoryMax`, `recent()` neu bauen)
    - `IniDoc historyIni() const;`

- [ ] **Step 1: Failing Tests schreiben**
  - `launch_plan_test` mit `MemFileSystem`, DOL über `test::dolImage`:
    - „gültiger Start“: Plan mit `dolPath == "sd:/apps/fceugx/boot.dol"`, `args[1] == "sd:/roms/nes/"`, `argvBlock` nicht leer, `dolSize` = Dateigröße
    - „DOL fehlt → NotFound mit DOL-Pfad“
    - „DOL 0 Byte bzw. Müll → InvalidData“
    - „DOL größer als kMaxDolBytes → InvalidData“: die Größe kommt über `DirEntry.size` aus `list` des DOL-Ordners; die Datei muss dafür nicht wirklich so groß sein
    - „ROM fehlt → NotFound mit ROM-Pfad“
    - „N64/GC/Wii → Unsupported mit Konsolenname“
    - „Preflight schreibt nichts“: `MemFileSystem`-Schreibzähler bleibt 0; den Zähler ergänzen, falls er fehlt
  - `library_store_test`:
    - „recordPlayed setzt an den Anfang, entfernt Duplikat, kappt bei 20, historyIni round-trip“
- [ ] **Step 2: Ausführen.** Erwartet: Kompilierfehler.
- [ ] **Step 3: Umsetzen.**
  - Die DOL-Größe kommt aus `list(Ordner der DOL)`, gesucht wird der Eintrag mit dem Dateinamen; fehlt er, ist das `NotFound`.
  - Der Header wird über `read(dol, 0, 0x100)` gelesen.
  - Die ROM gilt als vorhanden, wenn `read(rom, 0, 1 Byte)` gelingt; 0-Byte-ROMs sind erlaubt.
  - `packArgvBlock` bekommt die Basis `kArgsAddr`.
- [ ] **Step 4: Ausführen.** Erwartet: grün.
- [ ] **Step 5: Commit** `feat(core): Preflight für Emulator-Start und Verlauf schreiben`

### Task 4: Dialog-Szene und Fehlertexte

**Files:**
- Create: `src/ui/dialog_scene.hpp/.cpp`, `src/ui/launch_text.hpp/.cpp`, `tests/host/ui/dialog_scene_test.cpp`, `tests/host/ui/launch_text_test.cpp`
- Modify: `src/ui/strings.hpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class DialogScene final : public Scene { public: void open(const char* title, const char* text); bool isOverlay() const override { return true; } … };` Die Szene kopiert Titel und Text in feste Puffer (64 bzw. 256 Byte, gekürzt mit „…“). A, B oder ein Klick auf „OK“ → `out.pop = true`.
  - `int launchErrorText(const Error& e, char* title, std::size_t titleCap, char* text, std::size_t textCap);`
  - Texte in `strings.hpp`:
    - `kStrLaunchFailed = "Start nicht möglich"`
    - `kStrEmuMissing = "Emulator nicht gefunden:\n%s"`
    - `kStrEmuInvalid = "Emulator-Datei ist ungültig:\n%s"`
    - `kStrRomMissing = "Spieldatei nicht gefunden:\n%s"`
    - `kStrUnsupported = "Starten für %s folgt in einer späteren Version."`
    - `kStrArgsTooLong = "Pfad zu lang für den Emulator."`
    - `kStrOk = "OK"`

- [ ] **Step 1: Failing Tests schreiben**
  - „launchErrorText: NotFound mit Pfad auf .dol → kStrEmuMissing mit Pfad“
  - „NotFound mit anderem Pfad → kStrRomMissing“
  - „Unsupported → Konsolenname im Text“
  - „Text länger als Puffer wird mit … gekürzt, nie überlaufen“
  - `DialogScene`: „A schließt (pop)“, „B schließt“, „Klick auf OK-Rechteck schließt, Klick daneben nicht“, „update/draw allokieren nicht“ (Muster aus `details_scene_test`)
- [ ] **Step 2: Ausführen.** Erwartet: Kompilierfehler.
- [ ] **Step 3: Umsetzen.** Gestaltung wie der USB-Dialog in `StartupScene`: Karte in der Mitte, Titel Medium, Text Regular mit Zeilenumbruch an `\n`, ein Akzent-Knopf.
- [ ] **Step 4: Ausführen.** Erwartet: grün.
- [ ] **Step 5: Commit** `feat(ui): Preflight-Dialog`

### Task 5: „Starten“ in der Detailansicht

**Files:**
- Modify: `src/ui/scene.hpp/.cpp` (`AppCommand::Launch`, `appCommandName` → `"launch"`), `src/ui/details_scene.hpp/.cpp`, `tests/host/ui/details_scene_test.cpp`, `tests/host/ui/scene_test.cpp`

**Interfaces:**
- Produces: A auf „Starten“ → `out.command = AppCommand::Launch; out.game = &game_;`. Der M2-Hinweis „Starten folgt mit M3“ entfällt samt `hintText()` und `kHintStart`.

- [ ] **Step 1: Failing Test schreiben**
  - „A auf Starten liefert Launch mit dem Spiel“
  - „Zeiger daneben + A liefert nichts“ (vorhandene Logik)
  - `appCommandName(AppCommand::Launch) == "launch"`
  - Die M2-Tests zum Hinweistext entfernen.
- [ ] **Step 2: Ausführen.** Erwartet: FAIL, weil kein Launch-Kommando kommt.
- [ ] **Step 3: Umsetzen.**
- [ ] **Step 4: Ausführen.** Erwartet: grün.
- [ ] **Step 5: Commit** `feat(ui): Starten löst AppCommand::Launch aus`

### Task 6: App-Booter und `argv_echo`

**Files:**
- Create: `src/launch/app_booter/booter.c`, `src/launch/app_booter/booter.ld`, `src/launch/app_booter/crt0.S`, `tests/wii/argv_echo.c`
- Modify: `CMakeLists.txt`, `.github/workflows/build.yml` (Artefakt `argv_echo.dol`), `tools/build_wii.py`, falls die Zielliste dort steht

**Interfaces:**
- Consumes: Layout aus Task 1. Am Booter-Einsprung liegt bei `kArgsAddr - 0x10` ein kleiner Kopf `{ u32 dolAddr; u32 dolSize; u32 argvAddr; u32 argvSize; }`, den der Chainloader schreibt.
- Produces:
  - Binärdatei `app_booter.bin`, eingebettet als `app_booter_bin` / `app_booter_bin_size`
  - DOL `build/wii/argv_echo.dol`

- [ ] **Step 1: Booter schreiben.**
  - Der Booter liest den DOL-Header bei `dolAddr`.
  - Er kopiert jede Sektion mit `size > 0` (Byte-Schleife, kein libc), nullt BSS und macht für jede Zielsektion `dcbst`/`sync`/`icbi`/`isync` in 32-Byte-Schritten.
  - Steht an `entry + 4` die Kennung `_arg` (0x5f617267), kopiert er nur den 24-Byte-Header des `argv`-Blocks nach `entry + 8` und flusht diesen Bereich. Die Kommandozeile bleibt bei `kArgsAddr + 24` liegen; `commandLine` zeigt schon dorthin, weil `packArgvBlock` mit der Basis `kArgsAddr` gebaut wurde. Der Bereich ist reserviert und wird von niemandem überschrieben.
  - Danach springt er per `mtctr`/`bctr` zu `entry`.
  - `crt0.S`: Stack auf `kReservedEnd - 0x10` setzen (wächst nach unten, weit weg von Code und `argv`), `main` aufrufen.
  - `booter.ld`: `. = 0x93300000;`, `.text` mit `crt0` zuerst, keine weiteren Sektionen außer `.rodata`, `.data`, `.bss`; Größe ≤ `0x7F0` per `ASSERT`.
- [ ] **Step 2: `argv_echo.c` schreiben.**
  - Ein libogc-Programm: `VIDEO_Init`, einfacher Konsolenmodus.
  - Für jedes Argument `SYS_Report("NL_ARGV %d %s\n", i, argv[i])`, vorher `NL_ARGV argc=<n>`.
  - Danach eine Endlosschleife mit `VIDEO_WaitVSync`.
- [ ] **Step 3: CMake.**
  - Ziel `nl_app_booter`: `add_executable` mit `-nostartfiles -nostdlib -ffreestanding -Os` und dem Linkerskript, danach `objcopy -O binary` → `app_booter.bin`. Das Ergebnis wird per `dkp_add_embedded_binary_library(nl_booter …)` eingebettet und in `nl_wii_target` gelinkt.
  - Ziel `argv_echo` über `add_executable` + `ogc_create_dol`.
- [ ] **Step 4: Bauen und prüfen.**
  - Run: `python tools/build_wii.py`
  - Erwartet: keine Warnungen; `build/wii/argv_echo.dol` existiert; `app_booter.bin` ≤ 2032 Byte (CMake-`ASSERT` greift sonst)
  - Direktstart: `python tools/dolphin_smoke.py --dol build/wii/argv_echo.dol --no-fixtures --marker "NL_ARGV argc="`. Erwartet: `SMOKE OK`. Das zeigt, dass Dolphin `SYS_Report` der Test-DOL loggt.
- [ ] **Step 5: Commit** `feat(launch): App-Booter und Test-DOL argv_echo`

### Task 7: Chainloader und Start in `main`

**Files:**
- Create: `src/launch/chainloader.hpp/.cpp`
- Modify: `src/app/main.cpp`, `src/platform/video.hpp/.cpp` (`shutdownVideo()`: Bild schwarz, `VIDEO_Flush`, zwei `VIDEO_WaitVSync`), `CMakeLists.txt` (`src/launch/chainloader.cpp` in `NL_WII_SOURCES`)

**Interfaces:**
- Consumes: `preflightEmulator`, `recordPlayed`/`historyIni` (Task 3), `DialogScene`/`launchErrorText` (Task 4), `AppCommand::Launch` (Task 5), `app_booter_bin` (Task 6)
- Produces:
  - `Result<void> loadDolToBuffer(FileSystem& fs, const LaunchPlan& plan);` liest die DOL nach `kDolBufferAddr` und prüft die gelesene Länge.
  - `[[noreturn]] void chainload(const LaunchPlan& plan);` schreibt den Kopf für den Booter, den `argv`-Block nach `kArgsAddr` und den Booter nach `kBooterAddr`. Dann folgen `DCFlushRange`/`ICInvalidateRange`, `SYS_ResetSystem(SYS_SHUTDOWN, 0, 0)`, `_CPU_ISR_Disable` und der Sprung.
  - `main` reserviert den Bereich als erste Anweisung mit `SYS_SetArena2Hi(reinterpret_cast<void*>(kDolBufferAddr))`. Prüfe vorher im Code, dass `SYS_GetArena2Hi() > kDolBufferAddr` gilt; sonst steht im Log `NL_WARN Arena2 schon belegt`, und Starten bleibt gesperrt.
  - `emulators.ini` wird beim Start geladen. Fehlt sie (`NotFound`), legt `main` sie mit `defaultEmulatorsText()` an, wie bei `config.ini`, und loggt `NL_EMULATORS default`, sonst `ok`.

- [ ] **Step 1: Ablauf für `AppCommand::Launch` in `main` umsetzen.**
  1. `preflightEmulator`. Bei Fehler: `NL_PREFLIGHT_FAIL <code> <key>`, dann `launchErrorText`, `dialog.open`, `scenes.push(&dialog)`; der Launcher läuft weiter.
  2. `store.recordPlayed(key, time(nullptr))`, `history.ini` schreiben, `NL_HISTORY_SAVED`.
  3. `NL_LAUNCH <key>`.
  4. `worker.cancelAndJoin()`.
  5. `loadDolToBuffer`. Scheitert das: Dialog „Emulator-Datei ist ungültig“, weiterlaufen.
  6. `flushLogFile`, `input.shutdown()`, `unmountAll()`, `shutdownVideo()`.
  7. `chainload(plan)`.
- [ ] **Step 2: Bauen.** Run: `python tools/build_wii.py`. Erwartet: keine Warnungen.
- [ ] **Step 3: Smoke ohne Start.** Run: `python tools/dolphin_smoke.py --marker NL_READY --marker "NL_LIBRARY games=12" --marker "NL_EMULATORS"`. Erwartet: `SMOKE OK`.
- [ ] **Step 4: Commit** `feat(launch): Chainloader und Emulator-Start aus der Detailansicht`

### Task 8: Autostart, Fixtures und Chainload-Test in Dolphin

**Files:**
- Modify:
  - `src/core/config.hpp/.cpp` (`std::string debugAutostart;` aus `[debug] autostart`)
  - `tests/host/core/config_test.cpp`
  - `src/app/main.cpp`: Nach `NL_READY` sucht `main` das Spiel mit dem Autostart-Schlüssel. Gefunden: `NL_AUTOSTART <key>` loggen und denselben Weg wie `AppCommand::Launch` gehen. Nicht gefunden: `NL_WARN autostart <key> fehlt`.
  - `tools/make_fixtures.py`: schreibt `newlauncher/emulators.ini`, in der alle fünf Konsolen auf `sd:/apps/argv_echo/boot.dol` mit `args = {dir}|{file}` zeigen.
  - `tools/deploy_dolphin.py`: kopiert `build/wii/argv_echo.dol` nach `apps/argv_echo/boot.dol`, wenn die Datei existiert.
  - `tools/dolphin_smoke.py`: `--autostart KEY` hängt `[debug]\nautostart = KEY\n` an die deployte `config.ini`.
  - Tests in `tools/tests/`

- [ ] **Step 1: Failing Tests schreiben**
  - `config_test`: „[debug] autostart wird gelesen, fehlt → leer“
  - `tools/tests`:
    - „make_fixtures schreibt emulators.ini mit argv_echo für nes/snes/gb/gbc/gba“
    - „smoke --autostart ergänzt config.ini, ohne Option bleibt sie unverändert“
    - „deploy kopiert argv_echo.dol nach apps/argv_echo/boot.dol“
- [ ] **Step 2: Ausführen.** Erwartet: FAIL.
- [ ] **Step 3: Umsetzen.**
- [ ] **Step 4: Ausführen.** Erwartet: Host- und Tools-Tests grün.
- [ ] **Step 5: Chainload-Test in Dolphin.**
  - Run: `python tools/build_wii.py && python tools/dolphin_smoke.py --autostart "nes/Zelda (Europe) (Rev 1).nes" --marker NL_READY --marker "NL_LAUNCH nes/Zelda (Europe) (Rev 1).nes" --marker "NL_ARGV argc=3" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/nes/" --marker "NL_ARGV 2 Zelda (Europe) (Rev 1).nes"`
  - Erwartet: `SMOKE OK`.
  - Fehlen nur die `NL_ARGV`-Marker (Dolphin loggt nach dem Chainload nicht), ist das das Risiko aus Spec §10. Dann gilt: systematisch debuggen (Booter erreicht? Bild? Dolphin-Log auf Exceptions). Bleibt die Ursache Dolphin selbst, ist das ein Stopp mit Rückfrage, weil die Rückfallebene „Chainload-Test nur auf Hardware“ die Abnahme verschiebt.
- [ ] **Step 6: Commit** `feat: Autostart und Chainload-Test mit argv_echo`

### Task 9: Selbsttest, README, Abnahme M3

**Files:**
- Modify: `src/app/selftest.cpp` (Skript), `README.md`, `docs/superpowers/specs/2026-11-09-wii-launcher-design.md` (nur falls eine Ruling die Spec ändert)

- [ ] **Step 1: Selbsttest erweitern.**
  - Nach dem bestehenden Ablauf: zur N64-Leiste, Details öffnen, A auf „Starten“ → Marker `NL_PREFLIGHT_FAIL Unsupported n64/…` und ein offener Dialog (`scenes.top()` ist der Dialog); A schließt ihn.
  - Danach geht es weiter wie bisher (Leerlauf-Fenster, Crash).
  - Die Frame-Nummern des Leerlauf-Fensters passen sich dem neuen Ablauf an.
- [ ] **Step 2: README.**
  - Abschnitt „Emulatoren“: `emulators.ini`, Platzhalter, Standardpfade, Chainload-Test mit `--autostart`
  - CI-Artefakt `argv_echo.dol`
- [ ] **Step 3: Abnahme.**
  - `cmake --build --preset host && ctest --preset host`; Tools-Tests
  - `python tools/build_wii.py` ohne Warnungen
  - Smoke: `NL_READY`, `NL_LIBRARY games=12`, `NL_EMULATORS`
  - Chainload-Test aus Task 8
  - Selbsttest mit den M2-Markern plus `NL_PREFLIGHT_FAIL Unsupported`
- [ ] **Step 4: Commit** `feat: Selbsttest für Preflight, README, Abnahme M3`
