# M13 System-Erkennung und Einrichtung Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Launcher erkennt Plattform, cIOS und installierte Emulatoren selbst, wählt sinnvolle Standards und führt neue Nutzer durch einen Einrichtungs-Assistenten.

**Architecture:**
- Erkennen und Entscheiden sind getrennt:
  - `platform/system_probe` und `platform/app_scan` sammeln Rohdaten;
  - `core/cios_scan`, `core/emu_profiles`, `core/sysinfo` und `core/setup_wizard` entscheiden plattformfrei und sind host-getestet.
- `kCiosSlot` entfällt: Der Slot steht im `WiiBootPlan`.
- `EmulatorTable` bleibt der INI-Leser. Neu ist `resolveEmulator()`, das INI, Erkennung und Spiel-Override in der Reihenfolge der Spec auflöst.

**Tech Stack:** C++20, libogc (ES/ISFS), doctest, Dolphin-Smoke.

**Spec:** `docs/superpowers/specs/2026-11-11-v2-universell-design.md` – 6, 13, 15; Basis-Spec 6.4–6.6, M9-Plan (cIOS).

**Basis-Branch:** `feat/m13-erkennung` von `main` nach M12.

## Global Constraints

- **Vorrang:** Eine Festlegung in einer INI schlägt die Erkennung. Die Erkennung schreibt nichts. Nur der Assistent schreibt, nach Bestätigung und mit `.bak`-Sicherung.
- **cIOS-Wahl** (Spec 6.2), das erste Zutreffende gilt:
  1. `games.ini [game ID6] ios`
  2. `config.ini [wii] ios`
  3. erster d2x unter 249, 250, 248
  4. d2x mit der höchsten Slotnummer
  5. 249, wenn dort `Other` liegt
  6. sonst kein cIOS, Dialog aus M9.
- **`sysinfo.txt`** enthält **keine** MAC-Adresse, Seriennummer, Konsolen-ID, IP-Adresse oder Gerätekennung.
- **vWii-Erkennung** nur mit AHBPROT: `(read32(0x0D8005A0) >> 16) == 0xCAFE`. Ohne AHBPROT gilt `Platform::Unknown`, das Verhalten ist dann wie auf der Wii.
- **v1-Installationen:**
  - Eine vorhandene `emulators.ini` gilt weiter.
  - Ein Abschnitt, dessen `dol` exakt der v1-Vorgabe entspricht und dessen Datei fehlt, gilt als „nicht festgelegt“. Dann greift die Erkennung.
  - Neue `emulators.ini` enthalten nur noch einen Kommentarkopf.
- **Texte** DE/EN; Wii-Build warnungsfrei ON/OFF; alle früheren Smokes grün.

## Review Focus

1. cIOS-Scan auf einer Wii mit Stub-IOS in 249 (TMD-Version `0xFF00`) und d2x in 250 → Wahl 250. Mit leerem Slot-Bereich → Dialog „cIOS fehlt“, kein Start mit Slot 0.
2. `apps/`-Ordner mit 200 Einträgen, kaputtem `meta.xml` (fehlendes `<name>`, 1 MB groß, kein UTF-8): Die Erkennung bleibt unter 1 s auf SD. Kaputte Einträge werden übersprungen.
3. Assistent-Abbruch (HOME, Strom weg zwischen `.bak` und Schreiben): Beim nächsten Start ist entweder die alte oder die neue Datei vollständig da. Geschrieben wird über `<datei>.tmp` und Umbenennen.
4. Emulator laut Erkennung vorhanden, `boot.dol` aber leer oder abgeschnitten: Der Preflight meldet `EmuInvalid` wie bisher. Die Erkennung prüft nur die Existenz.
5. `[game ID6] ios = 249`, aber 249 fehlt: Dialog „cIOS fehlt“ mit Slot 249. Kein stiller Wechsel auf ein anderes cIOS, die Festlegung gilt.

---

### Task 1: Spike RetroArch-Argumente

**Files:**
- Create: `docs/superpowers/spikes/m13-retroarch.md`

- [ ] **Step 1:** Im RetroArch-Quelltext (`frontend/drivers/platform_gx.c`, Tag der aktuellen Wii-Version) nachlesen:
  - wie `argc`/`argv` ausgewertet werden;
  - ob `argv[1]` als Inhaltspfad gilt;
  - welches Pfadformat erwartet wird (`sd:/…` oder `usb:/…`);
  - ob ein Kern-DOL direkt startbar ist.
- [ ] **Step 2:** Ergebnis mit Quellzeilen und Commit ins Spike-Dokument.
  - **Positiv:** Argument-Schema notieren (z. B. `{path}`).
  - **Negativ:** Ruling „RetroArch-Profile entfallen“, Task 3 lässt sie weg.
  - Ein Dolphin-Gegentest ist optional und nur mit selbst erzeugtem Test-ROM erlaubt; nie ein kommerzielles ROM.
- [ ] **Step 3: Commit** `docs: Spike RetroArch-Argumente`

### Task 2: cIOS-Scan und Wahl

**Files:**
- Create: `src/core/cios_scan.hpp`, `src/core/cios_scan.cpp`, `tests/host/core/cios_scan_test.cpp`
- Modify:
  - `src/platform/wii_disc.*`: `readCiosStatus(u8 slot)` sowie `scanCios()` über 200–255, mit `ES_GetTitleIDs`-Liste statt 56 Einzelanfragen;
  - `src/core/launch_plan.*`: `WiiBootPlan::iosSlot`; `preflightWii` bekommt die Liste und die Festlegungen;
  - `src/launch/wii_boot.cpp`: `kCiosSlot` entfernen, `IOS_ReloadIOS(plan.iosSlot)`;
  - `src/core/config.*`: `[wii] ios`;
  - `src/ui/strings.cpp`: Dialogtext nennt den Slot.

**Interfaces:**
- Produces:
  ```cpp
  enum class CiosKind : u8 { D2x, Stub, Other };
  struct CiosEntry { u8 slot; CiosKind kind; u32 base; u16 titleVersion; std::string name; };
  CiosKind classifySlot(u16 titleVersion, u32 contentCount, const std::optional<IosInfo>& info);  // 0xFF00 oder 1 Content → Stub
  struct CiosChoice { std::optional<u8> slot; bool forced; };                                      // forced: aus INI
  CiosChoice chooseCios(std::span<const CiosEntry> found, std::optional<u8> gameIos, std::optional<u8> configIos);
  std::optional<u8> parseIosValue(std::string_view v);   // "auto" → nullopt; "200".."255" → Wert; sonst nullopt + NL_OPT_BAD
  ```
- Mit `forced` und fehlendem Slot liefert `preflightWii` `CiosMissing` mit Kontext Slot. Es gibt keinen Rückfall (Review Focus 5).

- [ ] **Step 1: Failing tests:**

  | Test | Erwartet |
  |---|---|
  | Stub in 249, d2x in 250 | 250 |
  | d2x in 248 und 251 | 248, weil 248 in der Liste 249/250/248 vorkommt |
  | nur d2x in 222 und 224 | 224 |
  | nur `Other` in 249 | 249 |
  | nur `Other` in 250 | nullopt |
  | `gameIos = 222` vorhanden | 222, `forced` |
  | `gameIos = 222` fehlt | Slot 222, `forced`; der Preflight meldet `CiosMissing` |
  | `configIos` schlägt die Automatik, verliert gegen `gameIos` | wie beschrieben |
  | `parseIosValue("199")`, `("256")`, `("abc")` | nullopt |

  Außerdem `launch_plan_test`: Plan trägt `iosSlot`.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün; M9-Dry-Run-Smoke zeigt `NL_WII_IOS 249` (Dolphin meldet installierte IOS aus dem Dev-NAND).
- [ ] **Step 5: Commit** `feat: cIOS-Scan 200–255 und Wahl nach Vorrang`

### Task 3: Emulator-Erkennung und Profile

**Files:**
- Create: `src/core/emu_profiles.hpp`, `src/core/emu_profiles.cpp`, `tests/host/core/emu_profiles_test.cpp`, `src/platform/app_scan.hpp`, `src/platform/app_scan.cpp`
- Modify:
  - `src/core/emulators.*`: v1-Vorgaben erkennen; neue Datei nur mit Kommentarkopf;
  - `src/core/launch_plan.cpp`: `preflightEmulator` nimmt den aufgelösten Eintrag;
  - `src/core/n64_choice.*`: `not64Dol`/`wii64Dol` aus der Auflösung;
  - `src/core/scanner.hpp`: `ScanRules::zipConsoles` aus den Profilen;
  - `src/core/emu_install.*` (M10): Ziel `sd:/apps/`, ohne SD `<data>`-Gerät.

**Interfaces:**
- Produces:
  ```cpp
  struct FoundApp { std::string device; std::string folder; std::string dol; std::string name; };  // aus <dev>:/apps/<folder>/
  struct EmuProfile { const char* id; const char* folder; u8 consoleMask; const char* args; bool acceptsZip; bool fatOnly; };
  std::span<const EmuProfile> builtinProfiles();   // fceugx, snes9xgx, vbagx, not64, wii64, nintendont, retroarch-<kern> (falls Spike positiv)
  std::span<const char* const> profileOrder(Console c);  // NES: fceugx, retroarch-fceumm; SNES: snes9xgx, retroarch-snes9x2010; GB/GBC: vbagx, retroarch-gambatte; GBA: vbagx, retroarch-mgba
  struct ResolvedEmu { std::string dol; std::string args; std::string profile; bool fromIni; };
  std::optional<ResolvedEmu> resolveEmulator(Console c, const EmulatorTable& ini, std::span<const FoundApp> found,
                                             std::optional<std::string_view> gameProfile);
  bool isV1Default(std::string_view section, std::string_view dol);   // exakt der v1-Standardpfad
  ```
- Argument-Schemata: fceugx/snes9xgx/vbagx `{dir}|{file}`, not64/wii64 `{path}`, RetroArch laut Spike.
- RetroArch-Kerne liegen als `apps/retroarch-wii/<kern>_libretro_wii.dol`. Ein `FoundApp` je Kern-DOL.
- `app_scan`:
  - liest höchstens 256 Ordner je Gerät;
  - liest `meta.xml` nur bis 64 KiB;
  - `<name>` wird per Suche gefunden, ohne XML-Bibliothek.

- [ ] **Step 1: Failing tests:**
  - Vorrang: INI-Abschnitt (nicht v1-Standard) > `gameProfile` > Profilreihenfolge;
  - v1-Standard-INI bei fehlender Datei: Erkennung greift;
  - v1-Standard-INI, Datei vorhanden: bleibt (`fromIni`);
  - nur RetroArch installiert → `retroarch-fceumm` für NES;
  - unbekanntes `gameProfile` → Reihenfolge + `NL_OPT_BAD emulator`;
  - App auf `usb2` gefunden → Pfad `usb2:/apps/…`;
  - `zipConsoles` aus den aufgelösten Profilen;
  - `isV1Default` für alle acht v1-Abschnitte.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Smoke.** Emulator-Smoke aus M3 grün (v1-INI), dazu eine Variante ohne `emulators.ini`: Erkennung findet FCEUGX in der SD-Fixture, `NL_EMU nes fceugx detected`.
- [ ] **Step 6: Commit** `feat: Emulator-Erkennung mit Profilen und Vorrang`

### Task 4: Systeminfo

**Files:**
- Create: `src/core/sysinfo.hpp`, `src/core/sysinfo.cpp`, `tests/host/core/sysinfo_test.cpp`, `src/platform/system_probe.hpp`, `src/platform/system_probe.cpp`, `src/ui/sysinfo_scene.hpp`, `src/ui/sysinfo_scene.cpp`
- Modify: `src/core/settings_model.*` (Zeile „Systeminfo“), `src/ui/strings.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class Platform : u8 { Wii, VWii, Unknown };
  struct SystemInfo {
    Platform platform; u16 sysMenuVersion; char region;      // 'U','E','J','K' oder '?'
    u32 launcherIos; bool ahbprot; std::vector<CiosEntry> cios;
    bool gcPorts;                                             // false auf vWii; die Wii mini wird nicht erkannt (Spec 6.1)
    std::vector<DeviceSummary> devices;                       // name, fs, total, free, games
    std::vector<ResolvedEmuSummary> emulators; bool netUp; bool timeSynced; bool tls;
  };
  char regionFromSysMenu(u16 version);                        // Tabelle 4.x/vWii-Versionen, sonst '?'
  std::string sysMenuLabel(u16 version);                      // 513 → "4.3U", 610 → "vWii E", unbekannt → "v<zahl>"
  std::string formatSysInfo(const SystemInfo& s);             // Text für sysinfo.txt, DE/EN nach Sprache
  ```
- **Step 1 (Daten):**
  - Versionstabelle des System-Menüs aus einer belegten Quelle ins Ledger:
    - 4.3: J 512, U 513, E 514, K 518;
    - vWii: J 608, U 609, E 610.
  - Andere Versionen erscheinen als Zahl.
- Die Szene listet die Felder. A = „Als Datei speichern“ schreibt `<data>/sysinfo.txt` über `.tmp` + Umbenennen.

- [ ] **Step 2: Failing tests:**
  - `regionFromSysMenu` für jede Tabellenzeile und eine unbekannte Version;
  - `formatSysInfo` enthält Plattform, cIOS-Tabelle, Geräte;
  - `formatSysInfo` enthält bei einer Eingabe mit gesetzten Netzwerkfeldern **kein** Muster `xx:xx:xx:xx:xx:xx` und keine IPv4-Adresse (Regex-Test).
- [ ] **Step 3:** FAIL. **Step 4:** implementieren. **Step 5:** Host-Suite grün.
- [ ] **Step 6: Smoke.** `[debug] open_scene = sysinfo` (aus M11) → `NL_SCENE sysinfo`. `[debug] sysinfo_save = 1` → Datei in der virtuellen SD, das Skript prüft sie auf MAC/IP-Muster.
- [ ] **Step 7: Commit** `feat: Systeminfo mit Datei zum Weitergeben`

### Task 5: Einrichtungs-Assistent

**Files:**
- Create: `src/core/setup_wizard.hpp`, `src/core/setup_wizard.cpp`, `tests/host/core/setup_wizard_test.cpp`, `src/ui/setup_scene.hpp`, `src/ui/setup_scene.cpp`
- Modify: `src/app/main.cpp` (Start ohne `config.ini` → Assistent), `src/core/settings_model.*` (Zeile „Einrichtung“), `src/ui/strings.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class WizardStep : u8 { Language, Sources, Cios, Emulators, Network, Done };
  class SetupWizard {
  public:
    SetupWizard(const SystemInfo& sys, const Config& current, std::span<const DeviceGames> gamesPerDevice);
    WizardStep step() const;
    void accept();   // Vorschlag übernehmen, weiter
    void skip();     // Standardwert, weiter
    void back();
    Config result() const;                 // Sprache, sources, [wii] ios (nur wenn ≠ Automatik), …
    std::optional<std::string> emulatorsIni() const;   // nur wenn der Nutzer eine Abweichung gewählt hat
  };
  std::string backupName(std::string_view path);        // "x/config.ini" → "x/config.ini.bak"
  ```
- Schritt „Netz“ reicht einen Auftrag an `NetService` (M11) ein: Verbindungstest gegen die Cover-Quelle. Bei TLS gehört der Zeitabgleich dazu.
- Schritt „Emulatoren“ bietet bei fehlenden Profilen den Sprung in die M10-Szene an.
- Schreiben am Ende:
  1. vorhandene Datei → `.bak`;
  2. neue Datei als `.tmp`;
  3. umbenennen.

- [ ] **Step 1: Failing tests:**
  - Durchlauf nur mit `accept()`: `result()` entspricht den Vorschlägen;
  - nur `skip()`: Standard-Config;
  - `back()` im ersten Schritt bleibt dort;
  - kein Gerät mit Spielen: Schritt Sources schlägt `auto` vor;
  - `emulatorsIni()` ohne Abweichung → nullopt;
  - `backupName`.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Smoke.**
  - Ohne `config.ini` gibt es keinen `[debug]`-Abschnitt. Deshalb liest der Launcher zusätzlich `<data>/debug.ini` (nur `[debug]`, sonst ignoriert).
  - `dolphin_smoke.py --fresh-data` legt nur diese Datei mit `wizard = accept` an; der Assistent nimmt dann jeden Vorschlag an.
  - Erwartet: `NL_WIZARD done`, `config.ini` geschrieben, zweiter Start ohne Assistent.
- [ ] **Step 6: Commit** `feat: Einrichtungs-Assistent`

### Task 6: Hardware-Abnahme M13

**Files:**
- Create: `docs/acceptance/m13-wii.md`

| Punkt | Prüfung |
|---|---|
| W1 | Systeminfo: System-Menü-Version, Region, d2x in 249 (Basis, Version) korrekt; `sysinfo.txt` ohne MAC/IP |
| W2 | Frische SD ohne `config.ini`: Assistent erscheint, Vorschläge passen, danach startet ein Wii-, ein GC- und ein NES-Spiel |
| W3 | `[wii] ios = 250` (wenn installiert) bzw. ein fehlender Slot → Dialog mit Slotnummer |
| W4 | Emulator nur auf USB (`usb:/apps/fceugx`): wird erkannt und startet |
| W5 | RetroArch (falls Spike positiv): NES-Spiel startet über `fceumm` |
| W6 | vWii (optional): Systeminfo zeigt „vWii“ |

- [ ] **Step 1:** Checkliste schreiben. **Step 2: Commit** `docs: Hardware-Abnahme M13`
