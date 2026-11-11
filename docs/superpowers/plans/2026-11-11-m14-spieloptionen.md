# M14 Spieloptionen und cIOS pro Spiel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Video-, Sprach-, Online- und cIOS-Einstellungen für Wii-Spiele sowie Nintendont-Einstellungen für GC-Spiele, global und pro Spiel, mit einer Options-Szene.

**Architecture:**
- `core/game_options` liest `[wii]`/`[gc]` und `[game <ID6>]`, löst den Vorrang auf und bildet die Werte ab:
  - auf ein plattformfreies Spiegelbild der benötigten `the_CFG`-Felder (`WiiCfgFields`);
  - auf `NinConfigInput`.
- `launch/wii_boot.cpp` kopiert nur noch `WiiCfgFields` in `the_CFG`; die festen `kNl*`-Standards entfallen.
- `core/options_model` beschreibt die Zeilen der Options-Szene und ist host-getestet. `ui/options_scene` zeichnet nur.

**Tech Stack:** C++20, doctest, Dolphin (Dry-Run-Marker).

**Spec:** `docs/superpowers/specs/2026-11-11-v2-universell-design.md` – 6.2, 7, 15; M8-Plan (`NIN_CFG`), M9-Plan (`the_CFG`).

**Basis-Branch:** `feat/m14-optionen` von `main` nach M13.

## Global Constraints

- **Vorrang:** Ein Schlüssel in `[game ID6]` schlägt `[wii]`/`[gc]`. `default` oder ein fehlender Schlüssel heißt: globaler Wert.
- **Unbekannte Werte** nehmen den Standard und loggen `NL_OPT_BAD <schlüssel>`.
- **Wii-Tabelle** genau nach Spec 7.2, Standards:
  - `video = system`, `video_patch = off`, `vipatch = 0`, `language = game`;
  - `country_patch = 0`, `region_patch = 0`, `aspect = game`, `deflicker = game`, `width = auto`;
  - `fix480p = 0`, `online = off`, `ios = auto`, `return_to = none`.
- **`returnTo` ist im Booter `u32`** (`booter_cfg.h`), also die untere Hälfte der Title-ID.
  - `return_to = <16 Hex>` ist nur gültig, wenn die obere Hälfte `00010001` ist (Kanal); sonst `NL_OPT_BAD return_to`.
  - `hbc` → `0x4C554C5A` (`LULZ`, HBC ≥ 1.1). Ist nur `00010001-4A4F4449` (`JODI`) installiert, nimmt die Plattform diesen.
  - Ohne d2x wird der Wert ignoriert.
- **GC-Tabelle** genau nach Spec 7.3, Standards:
  - `video = auto`, `progressive = 0`, `force_wide = 0`, `wiiu_wide = 0`;
  - `memcard = emu`, `memcard_blocks = 251`, `native_si = 0`;
  - `language = auto`, `max_pads = 4`, `skip_ipl = 0`.
- **Ohne Optionen** sind `the_CFG` und `NIN_CFG` byte-gleich zu M9/M8 (Regressionstest).
- **Texte** DE/EN; Wii-Build warnungsfrei ON/OFF; alle früheren Smokes grün.

## Review Focus

1. `games.ini` mit `[game RMGP01]` doppelt, Schlüsseln in Großschreibung (`Video = PAL60`) oder Werten mit Leerzeichen: Abschnitte werden zusammengeführt (der letzte Wert gewinnt), Schlüssel und Werte ohne Groß-/Kleinschreibung, getrimmt.
2. Eine GC-Option, die die erkannte Nintendont-Version nicht kennt (z. B. `skip_ipl` auf einer alten Version): Die Zeile ist in der Szene ausgegraut, der Wert wird nicht gesetzt, Log `NL_OPT_UNSUPPORTED skip_ipl`.
3. `memcard = real` auf vWii/Wii mini oder `native_si = 1` ohne GC-Ports: Preflight-Hinweis, kein Abbruch.
4. Options-Szene: „Zurücksetzen“, dann sofort B: Der Abschnitt ist gelöscht. Der Rest der `games.ini` (N64-Overrides, Emulator-Profile) bleibt unverändert, Kommentare bleiben.
5. `language = de` bei einem NTSC-J-Spiel: Der Wert wird gesetzt wie gewählt. Die Spec sieht keine Sperre vor; die Detailansicht warnt nicht. Das ist ein Ruling, kein Fehler.

---

### Task 1: Nintendont-Bits und Mindestversionen festhalten

**Files:**
- Modify: `src/core/nin_cfg.hpp`, `src/core/nin_cfg.cpp`, `tests/host/core/nin_cfg_test.cpp`

**Interfaces:**
- Produces (Werte aus Step 1):
  ```cpp
  // je eine inline constexpr u32, Wert aus Step 1:
  // kNinCfgForceWide, kNinCfgForceProg, kNinCfgMcMulti, kNinCfgNativeSi, kNinCfgWiiuWide, kNinCfgSkipIpl, kNinCfgCheats, kNinCfgCheatPath
  // kNinVidAuto, kNinVidForce, kNinVidNone, kNinVidPal50, kNinVidPal60, kNinVidNtsc, kNinVidMpal, kNinVidProg
  // kNinLangAuto; Sprachwerte en/de/fr/es/it/nl ebenfalls aus Step 1
  struct NinConfigInput { /* bisher */ u32 extraFlags = 0; u32 videoMode = 0; u32 language = kNinLangAuto; u32 maxPads = 4; u32 memcardBlocks = 2; /* Index 0–5 */ };
  struct NinOptionGate { const char* key; NinVersion minVersion; };
  std::span<const NinOptionGate> ninOptionGates();
  ```

- [ ] **Step 1: Quelle lesen.**
  - `common/include/CommonConfig.h` aus dem Nintendont-Repo, Stand der Version, die M8 als Minimum verlangt, und der aktuellen. Commit-Hashes ins Ledger.
  - Daraus übernehmen: alle Bits, Video-Werte, Sprachwerte, `MemCardBlocks`-Kodierung, `MaxPads`.
  - Mindestversion je Option per `git log -S <NAME>` bestimmen.
  - Weicht ein Wert zwischen den Versionen ab, ist das eine Ruling-Pflicht. Es gilt Spec 15: Die Version begrenzt die angebotenen Optionen.
- [ ] **Step 2: Failing tests:**
  - jede Konstante gegen den Wert aus Step 1 (wie M8 es für `MEMCARDEMU`/`AUTO_BOOT`/`USB` tut);
  - `buildNinCfg` mit `extraFlags = kNinCfgForceWide`, `videoMode = kNinVidForce|kNinVidPal60`, `language = 1`, `maxPads = 2`, `memcardBlocks = 3` → Bytes an den Offsets aus `CommonConfig.h`;
  - Standardeingabe → byte-gleich zum M8-Golden.
- [ ] **Step 3:** FAIL. **Step 4:** implementieren. **Step 5:** Host-Suite grün.
- [ ] **Step 6: Commit** `feat(core): Nintendont-Optionen mit belegten Bitwerten`

### Task 2: Optionen auflösen und abbilden

**Files:**
- Create: `src/core/game_options.hpp`, `src/core/game_options.cpp`, `tests/host/core/game_options_test.cpp`
- Modify: `src/core/ini.hpp/.cpp` (`removeSection`, Schlüsselvergleich ohne Groß-/Kleinschreibung nur für `[game …]`-Leser über eine Hilfsfunktion; bestehendes Verhalten unverändert)

**Interfaces:**
- Produces:
  ```cpp
  struct WiiCfgFields { u8 vidMode, patchVidMode, vipatch, language /*0xCD = game*/, countryString, patchregion;
                        s8 aspectRatio; u8 deflicker, videoWidth, patchFix480p, privateServer; u32 returnTo; };
  struct WiiOptions { WiiCfgFields cfg; std::optional<u8> ios; bool returnToHbc; };
  struct GcOptions  { NinConfigInput nin; bool memcardReal; bool nativeSi; std::vector<std::string> unsupported; };
  WiiOptions resolveWiiOptions(const IniDoc& config, const IniDoc& games, std::string_view id6);
  GcOptions  resolveGcOptions(const IniDoc& config, const IniDoc& games, std::string_view id6, NinVersion nin);
  struct OptionValue { std::string_view key; std::string value; bool inherited; std::string inheritedValue; };
  std::vector<OptionValue> listOptions(Console c, const IniDoc& config, const IniDoc& games, std::string_view id6);
  ```
- Abbildung genau nach Spec 7.2/7.3:
  - `video` game/system/pal50/pal60/ntsc/progressive → 0–5;
  - `language` game → `0xCD`, ja … ko → 0–9;
  - `aspect` game/4:3/16:9 → −1/0/1;
  - `online` off/nossl/wiimmfi → 0–2;
  - `memcard_blocks` 59/123/251/507/1019/2043 → 0–5;
  - `memcard = multi` → `MC_MULTI` + `MEMCARDEMU`; `real` → keins von beiden.

- [ ] **Step 1: Failing tests:**
  - jede Zeile beider Tabellen mit jedem Wert (tabellengetrieben);
  - Vorrang Spiel > global > Standard;
  - `default` im Spiel → global;
  - `Video = PAL60` (Groß) → pal60;
  - Unsinn → Standard + `NL_OPT_BAD` (Log-Fänger aus den Host-Tests);
  - doppelter Abschnitt → letzter Wert;
  - `return_to = hbc` → `0x4C554C5A`;
  - `return_to = 0001000148415858` → `0x48415858`;
  - `return_to = 0000000100000002` → Standard + `NL_OPT_BAD`;
  - GC-Option unter der Mindestversion → in `unsupported`, Wert nicht gesetzt;
  - `listOptions` markiert geerbte Werte;
  - Standard-Ergebnis Wii → identisch zu den bisherigen `kNl*`-Werten.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Commit** `feat(core): Spieloptionen Wii/GC mit Vorrang und Abbildung`

### Task 3: Start mit Optionen

**Files:**
- Modify:
  - `src/launch/wii_boot.hpp/.cpp`: `prepareWiiBoot(plan, WiiCfgFields)`; `kNl*`-Standards nur noch als Testvergleich in `booter_cfg.h`; HBC-Rückkehr über `ES_GetTitleIDs` prüfen (`LULZ`/`JODI`);
  - `src/core/launch_plan.*`: `WiiBootPlan` trägt `WiiOptions`; `preflightGame` für GC trägt `GcOptions` in `NIN_CFG`; Preflight-Hinweise `memcard = real`/`native_si` ohne GC-Ports als `LaunchWarning`;
  - `src/app/main.cpp`;
  - `src/ui/launch_text.cpp`.

**Interfaces:**
- Consumes: `resolveWiiOptions`, `resolveGcOptions` (Task 2), `chooseCios` (M13) mit `WiiOptions::ios` als `gameIos`.
- Produces:
  - Log `NL_WII_CFG vid=%u vpatch=%u lang=%02x aspect=%d defl=%u width=%u online=%u ios=%u ret=%08x`;
  - Log `NL_NIN_CFG flags=%08x vid=%08x lang=%u blocks=%u pads=%u`. Beide werden auch im Dry-Run geschrieben.

- [ ] **Step 1: Failing tests** in `launch_plan_test`:
  - GC mit `[gc] force_wide = 1` → `NIN_CFG`-Flags enthalten `kNinCfgForceWide`;
  - Wii mit `[game RMGP01] ios = 250` und 250 vorhanden → `iosSlot = 250`;
  - `memcard = real` mit `gcPorts = false` → Warnung, Plan trotzdem gültig.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün, Wii-Build 0 Warnungen.
- [ ] **Step 5: Smokes.**
  - M9-Dry-Run mit `games.ini` `[game <fixture-ID6>] video = pal60` und `language = de` → `NL_WII_CFG vid=3 … lang=02`.
  - M8-GC-Smoke mit `[gc] progressive = 1` → `NL_NIN_CFG` zeigt das Prog-Bit.
  - Ohne Optionen sind beide Marker gleich den Werten aus M8/M9.
- [ ] **Step 6: Commit** `feat: Wii- und GC-Start mit Spieloptionen`

### Task 4: Options-Szene

**Files:**
- Create: `src/core/options_model.hpp`, `src/core/options_model.cpp`, `tests/host/core/options_model_test.cpp`, `src/ui/options_scene.hpp`, `src/ui/options_scene.cpp` (nur Zeichnen und Eingabe; alle Logik im Modell)
- Modify: `src/ui/input.hpp/.cpp` (neue `Action::Options` vor `Count`: Wiimote `+` und GC `R` lösen sie zusätzlich zu `NextConsole` aus; Test in `tests/host/ui/input_test.cpp`), `src/ui/details_scene.cpp` (Knopf „Optionen“ über `Action::Options`), `src/core/settings_model.*` (Zeilen „Wii-Spiele“, „GameCube-Spiele“), `src/ui/strings.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class OptionsModel {
  public:
    OptionsModel(Console c, std::string id6 /* leer = global */, IniDoc config, IniDoc games, NinVersion nin);
    std::size_t rows() const; std::string label(std::size_t r) const; std::string value(std::size_t r) const; // "Standard (PAL 60)"
    bool enabled(std::size_t r) const;   // false bei unsupported
    void step(std::size_t r, int dir);   // Werteliste inkl. "default" an Position 0 (nur pro Spiel)
    void reset();                        // Abschnitt [game ID6] entfernen
    bool dirty() const;
    std::string gamesIniText() const; std::string configIniText() const;
  };
  ```
- Gespeichert wird beim Schließen der Szene über `.tmp` + Umbenennen, nur wenn `dirty()` gilt.

- [ ] **Step 1: Failing tests:**
  - Zeilenzahl Wii 13, GC 10;
  - Wert ohne Eintrag pro Spiel → `Standard (…)`;
  - `step` nach links an Position 0 läuft auf den letzten Wert;
  - `reset` → Abschnitt fehlt; N64-Override-Abschnitt im selben Text bleibt Byte für Byte;
  - globale Szene zeigt kein `Standard (…)`;
  - unsupported-Zeile `enabled == false`.
- [ ] **Step 2:** FAIL. **Step 3:** implementieren. **Step 4:** Host-Suite grün.
- [ ] **Step 5: Smoke.** `[debug] open_scene = options:<ID6>` → `NL_SCENE options`; `[debug] option_set = video=pal60` setzt einen Wert und schließt die Szene; danach steht in `games.ini` `video = pal60`.
- [ ] **Step 6: Commit** `feat(ui): Options-Szene pro Spiel und global`

### Task 5: Hardware-Abnahme M14

**Files:**
- Create: `docs/acceptance/m14-wii.md`

| Punkt | Prüfung |
|---|---|
| W1 | PAL-Spiel mit `video = pal60` läuft in 60 Hz |
| W2 | `language = en` bei einem deutschen Spiel: Spiel auf Englisch |
| W3 | `online = wiimmfi` bei Mario Kart Wii: Wiimmfi-Verbindung (falls Netz verfügbar) |
| W4 | `ios = 250` (falls installiert) startet über 250 |
| W5 | `return_to = hbc`: nach dem Beenden zurück im HBC |
| W6 | GC: `force_wide = 1` breit, `memcard_blocks = 1019` legt eine größere Karte an, `progressive = 1` mit 480p-Kabel |
| W7 | Zurücksetzen in der Szene entfernt die Werte, Spiel startet mit Standard |

- [ ] **Step 1:** Checkliste schreiben. **Step 2: Commit** `docs: Hardware-Abnahme M14`
