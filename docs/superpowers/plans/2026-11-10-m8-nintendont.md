# M8 – Nintendont-Backend: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GameCube-Spiele aus `games/*/game.iso` starten direkt in Nintendont. Der Launcher übergibt einen `NIN_CFG`-Block als `argv[1]`, ohne IOS-Reload, über den bestehenden Chainloader.

**Architecture:**
- Plattformfrei in `core/`:
  - `core/nin_cfg` baut den 548-Byte-Block (Big-Endian).
  - `findNintendontVersion` liest die Version aus der Nintendont-DOL.
- `preflightGame` bekommt einen GC-Zweig. Er prüft DOL, Version, Spieldatei, Pfadlänge und AHBPROT und liefert einen normalen `LaunchPlan` mit `args = {dolPath, Blob}`.
- Der Chainloader aus M3 bleibt unverändert, denn `packArgvBlock` ist binärsicher.
- `argv_echo` erkennt den Block und gibt ihn dekodiert aus; so prüft der Dolphin-Smoke-Test die Übergabe.

**Tech Stack:** wie M7. Keine neuen Bibliotheken.

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md`, Abschnitte:
- §1 (GameCube → Nintendont)
- §5.1 (`games/<Titel> [ID6]/game.iso`), §5.4 (`[nintendont] dol`)
- §6.1 (AHBPROT über `<ahb_access/>`), §6.2 (GC-Scanner), §6.5 (Backend `Nintendont`, „Details im Plan zu M8“)
- §9 M8, §10 (Risiko `NIN_CFG`-Version)

**Basis:** Branch `feat/m8-nintendont` von `main` nach dem Merge von M7.

## Global Constraints

- Alle Global Constraints aus M1–M7 gelten weiter.
- **`NIN_CFG`**, wie in Nintendont `common/include/CommonConfig.h`:
  - Größe genau 548 Byte (0x224), alle Mehrbyte-Felder Big-Endian.
  - **Gesendet wird Version 10.** Nintendont ab 6.493 nimmt v10 an. Neuere Versionen (v11 auf master) heben ältere Blöcke selbst an (`UpdateNinCFG`); das Layout von v10 und v11 ist identisch.

  | Offset | Feld | Wert vom Launcher |
  |---|---|---|
  | 0x000 | Magicbytes | `0x01070CF6` |
  | 0x004 | Version | `10` |
  | 0x008 | Config | `NIN_CFG_MEMCARDEMU (1<<3) \| NIN_CFG_AUTO_BOOT (1<<7)`, bei Bibliothek auf USB zusätzlich `NIN_CFG_USB (1<<10)` → SD `0x00000088`, USB `0x00000488` |
  | 0x00C | VideoMode | `0` (`NIN_VID_AUTO`) |
  | 0x010 | Language | `0xFFFFFFFF` (`NIN_LAN_AUTO`) |
  | 0x014 | GamePath[255] | `/` + Pfad relativ zum Bibliotheksgerät, ohne `sd:`/`usb:`, NUL-terminiert, Rest 0 |
  | 0x113 | CheatPath[255] | alles 0 |
  | 0x212 | Padding (2) | 0 |
  | 0x214 | MaxPads | `4` |
  | 0x218 | GameID | erste 4 Zeichen der ID6 als BE-u32 (`GM8P` → `0x474D3850`) |
  | 0x21C | MemCardBlocks (u8) | `2` (251 Blöcke) |
  | 0x21D | VideoScale (s8) | `0` |
  | 0x21E | VideoOffset (s8) | `0` |
  | 0x21F | NetworkProfile (u8) | `0` |
  | 0x220 | WiiUGamepadSlot (u32) | `0` |

- **Version in der DOL:** ASCII `$$Version:` gefolgt von `<major>.<minor>` (z. B. `$$Version:6.509`). Mindestversion **6.493**.
- `GamePath` höchstens 254 Byte (255 mit NUL), sonst Fehler.
- AHBPROT gilt als aus, wenn `read32(0x0D800064) == 0xFFFFFFFF`.
- `argv[0]` = DOL-Pfad wie bei allen Backends (Nintendont leitet daraus seinen Startordner ab).
- Neue Marker:
  - `NL_AHBPROT off|on`: beim Start
  - `NL_NIN ver=<major>.<minor> path=<GamePath> usb=<0|1>`: vor dem Chainload
  - `NL_ARGV_NINCFG ver=<n> cfg=0x%08X vid=0x%08X lang=<s32> pads=<n> mc=<n> id=<4 Zeichen> path=<GamePath>`: aus `argv_echo`
- Fehlerdialoge über die Sprachtabelle (M6), DE und EN.

## Review Focus

1. **Alte Nintendont-Version oder DOL ohne Versionsstring** (Fork, beschädigt, falsche Datei unter `boot.dol`). Erwartet:
   - Unter 6.493 kommt der Dialog „Nintendont zu alt (gefunden x.y, nötig 6.493)“.
   - Ohne Versionsstring kommt der Dialog „Nintendont nicht erkannt“ statt eines Starts mit falschem Block (Task 2).
2. **Der Versionsstring liegt über einer Lesegrenze** (die Suche liest in Blöcken). Erwartet: Er wird trotzdem gefunden; ein Test legt ihn genau auf die Blockgrenze (Task 2).
3. **Sonderzeichen und Länge im Pfad** (Umlaute, `&`, `'`, eckige Klammern, 254 und 255 Byte). Erwartet:
   - Der Pfad landet bytegenau als UTF-8 im Block.
   - Ab 255 Byte erscheint der Dialog „Pfad zu lang“ und es gibt keinen abgeschnittenen Pfad (Task 1, Task 2).
4. **Launcher ohne AHBPROT-Freigabe** (per wiiload gestartet oder `meta.xml` ohne `<ahb_access/>`). Erwartet: Preflight scheitert mit „Launcher ohne AHB-Zugriff gestartet – bitte über den Homebrew Channel starten“; Wii-, Retro- und VC-Starts sind nicht betroffen (Task 2).
5. **GC-Spiel auf SD bei Bibliothek auf USB und umgekehrt, Disc 2.** Erwartet:
   - Das USB-Bit folgt allein dem Bibliotheksgerät.
   - `disc2.iso` braucht keine Angabe, Nintendont findet sie neben `game.iso` (Task 1: USB-Bit; Task 4: Abnahme mit Zwei-Disc-Spiel, falls vorhanden).

## Abweichungen und Festlegungen gegenüber der Spec

| Punkt | Festlegung in M8 | Grund |
|---|---|---|
| „Struktur und Version entsprechen der installierten Nintendont-Version“ (§6.5) | immer v10 senden, ab 6.493 starten; ältere Versionen ablehnen | Nintendont hebt v10 selbst auf neuere Versionen an; ein fester Block ist testbar und deckt alle Versionen seit 2021 ab |
| Videomodus und Controller (§6.5) | VideoMode `NIN_VID_AUTO`, `MaxPads = 4`. Progressive Ausgabe setzt Nintendont selbst aus den Systemeinstellungen | Keine Spieloptionen in v1; die Automatik von Nintendont ist der sichere Standard |
| Spieloptionen für GC | keine; der Knopf „Optionen“ in der Detailansicht bleibt für GC ausgeblendet (wie in M6) | Nicht in der Spec gefordert; Kandidat für v2 |
| Cheats, Widescreen-Hack, BBA | aus | dito |
| Dolphin-Abnahme | `argv_echo` statt Nintendont; AHBPROT im Smoke-Test über `[debug] ignore_ahbprot = 1` überbrückt | Nintendont läuft in Dolphin nicht; Dolphins AHBPROT-Register ist nicht verlässlich. Der Debug-Schlüssel wirkt nur, wenn er in `config.ini` steht |

## Dateistruktur

```
src/core/nin_cfg.hpp/.cpp      NinConfigInput, buildNinCfg, NinVersion, findNintendontVersion, kMinNintendont
src/core/launch_plan.hpp/.cpp  LaunchFailure: NinMissing, NinTooOld, NinUnknown, NinPathTooLong, AhbProt; GC-Zweig
src/core/emulators.*           EmulatorTable::nintendont()
src/core/config.*              debugIgnoreAhbprot
src/platform/system.hpp/.cpp   ahbprotDisabled()
src/ui/strings.hpp, launch_text.cpp   Dialogtexte
src/app/main.cpp               AHBPROT-Marker, NL_NIN
tests/wii/argv_echo.c          NL_ARGV_NINCFG
tests/fixtures/sd/newlauncher/emulators.ini   [nintendont] dol = sd:/apps/argv_echo/boot.dol
docs/hardware-acceptance.md    Abschnitt G (GameCube)
```

---

### Task 1: `NIN_CFG`-Block

**Files:**
- Create:
  - `src/core/nin_cfg.hpp/.cpp`
  - Test: `tests/host/core/nin_cfg_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `inline constexpr std::size_t kNinCfgSize = 548;`
  - `inline constexpr u32 kNinCfgMagic = 0x01070CF6, kNinCfgVersion = 10;`
  - Bits als `inline constexpr u32`: `kNinCfgMemcardEmu = 1u << 3`, `kNinCfgAutoBoot = 1u << 7`, `kNinCfgUsb = 1u << 10`
  - `struct NinConfigInput { std::string_view gamePath; std::string_view id6; bool usb; };`: `gamePath` mit führendem `/`
  - `Result<std::array<u8, kNinCfgSize>, NinCfgError> buildNinCfg(const NinConfigInput&)`
    - `enum class NinCfgError : u8 { PathTooLong, BadId }`
    - `PathTooLong` bei `gamePath.size() > 254`
    - `BadId` bei `id6.size() < 4`
  - `std::string ninGamePath(std::string_view relPath)`: genau ein führendes `/`, `\` wird zu `/`

- [ ] **Step 1: Write the failing tests:**
  - `"Golden-Block SD"`:
    - Eingabe `{"/games/Metroid Prime [GM8P01]/game.iso", "GM8P01", false}`
    - Bytes an den Offsets aus der Tabelle der Global Constraints:
      - `0x000..0x003 == 01 07 0C F6`
      - `0x004..0x007 == 00 00 00 0A`
      - `0x008..0x00B == 00 00 00 88`
      - `0x00C..0x00F == 0`
      - `0x010..0x013 == FF FF FF FF`
      - `0x014..` Pfad + NUL
      - `0x113..0x211` alles 0
      - `0x214..0x217 == 00 00 00 04`
      - `0x218..0x21B == 47 4D 38 50`
      - `0x21C == 02`, `0x21D..0x21F == 0`
      - `0x220..0x223 == 0`
  - `"USB setzt Bit 10"`: Config `== 00 00 04 88`
  - `"Pfad mit 254 Byte geht, 255 nicht"`
  - `"UTF-8 bytegenau"`: `"/games/Pokémon Colosseum [GC6P01]/game.iso"`
  - `"ninGamePath"`:
    - `"games/x/game.iso"` → `"/games/x/game.iso"`
    - `"/games/x/game.iso"` bleibt
    - `"games\\x\\game.iso"` → `"/games/x/game.iso"`
- [ ] **Step 2: Run** `ctest --preset host -R nin_cfg`. Expected: FAIL (Header fehlt).
- [ ] **Step 3: Implement** wie oben. Byte-Schreiber aus `core/bin_io` wiederverwenden.
- [ ] **Step 4: Run** dieselben Tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat: NIN_CFG v10 bauen`

### Task 2: Nintendont-Version und Preflight

**Files:**
- Modify:
  - `src/core/nin_cfg.hpp/.cpp`
  - `src/core/launch_plan.hpp/.cpp`
  - `src/core/emulators.hpp/.cpp`
  - `src/ui/strings.hpp`, `src/ui/launch_text.cpp`
  - `src/app/main.cpp`: Aufruf von `preflightGame`
- Test:
  - `tests/host/core/nin_cfg_test.cpp`
  - `tests/host/core/launch_plan_test.cpp`
  - `tests/host/ui/launch_text_test.cpp`
  - Hilfe `tests/host/support/dol_images.hpp` (bestehend; um eingebetteten Text erweitern)

**Interfaces:**
- Consumes: `buildNinCfg`, `ninGamePath` (Task 1); `FileSystem::read(file, offset, span)`; `packArgvBlock`; `fileSize`
- Produces:
  - `struct NinVersion { u16 major, minor; auto operator<=>(const NinVersion&) const = default; };`
  - `inline constexpr NinVersion kMinNintendont{6, 493};`
  - `std::optional<NinVersion> findNintendontVersion(FileSystem&, std::string_view dolPath)`:
    - liest in Blöcken zu 64 KiB mit 16 Byte Überlappung
    - erstes Vorkommen von `$$Version:` mit `\d+\.\d+` dahinter; sonst `nullopt`
  - `std::string formatNinVersion(NinVersion)`: `"6.509"`
  - `const EmulatorEntry& EmulatorTable::nintendont() const`: `[nintendont] dol`; Standard `sd:/apps/nintendont/boot.dol`, `args` wird ignoriert
  - `LaunchFailure` neu, Namen für `NL_PREFLIGHT_FAIL`:

    | Wert | Name |
    |---|---|
    | `NinMissing` | `nin_missing` |
    | `NinUnknown` | `nin_unknown` |
    | `NinTooOld` | `nin_too_old` |
    | `NinPathTooLong` | `nin_path_too_long` |
    | `AhbProt` | `ahbprot` |

  - `preflightGame(fs, g, libRoot, emus, n64, bool ahbprotOff)`: neuer Parameter ohne Standardwert. Für `Console::Gc` gilt in dieser Reihenfolge:
    1. `!ahbprotOff` → `AhbProt`
    2. DOL fehlt → `NinMissing` (Kontext DOL-Pfad)
    3. DOL ungültig → `EmuInvalid`
    4. keine Version → `NinUnknown`
    5. `< kMinNintendont` → `NinTooOld` (Kontext `"<gefunden>"`)
    6. Spieldatei fehlt → `RomMissing`
    7. Pfad > 254 → `NinPathTooLong`
    8. sonst `LaunchTarget{Dol}` mit `args = {dolPath, std::string(blob.begin(), blob.end())}`; `usb` = `libRoot` beginnt mit `usb:`
  - Nicht-GC-Konsolen ignorieren `ahbprotOff`.
  - `LaunchTarget` bekommt `std::optional<NinVersion> nintendont` für den Marker `NL_NIN`.
  - Dialogtexte, DE / EN; `%s` laut Task 1 aus M6 in beiden gleich:

    | Fehler | DE | EN |
    |---|---|---|
    | `NinMissing` | „Nintendont fehlt“ / „Nicht gefunden: %s“ | „Nintendont missing“ / „Not found: %s“ |
    | `NinUnknown` | „Nintendont nicht erkannt“ / „%s enthält keine Versionsangabe.“ | „Nintendont not recognized“ / „%s contains no version string.“ |
    | `NinTooOld` | „Nintendont zu alt“ / „Gefunden %s, nötig 6.493 oder neuer.“ | „Nintendont too old“ / „Found %s, need 6.493 or newer.“ |
    | `NinPathTooLong` | „Pfad zu lang“ / „Nintendont erlaubt höchstens 254 Zeichen: %s“ | „Path too long“ / „Nintendont allows at most 254 characters: %s“ |
    | `AhbProt` | „Kein Hardwarezugriff“ / „Bitte den Launcher über den Homebrew Channel starten.“ | „No hardware access“ / „Please start the launcher from the Homebrew Channel.“ |

    Der `AhbProt`-Text hat kein `%s`; `fill` lässt ihn unverändert.

- [ ] **Step 1: Write the failing tests:**
  - `nin_cfg_test`:
    - `"Version gefunden"`: DOL-Bild mit `$$Version:6.509` bei Offset 100 → `{6, 509}`
    - `"Version auf der Blockgrenze"`: `$$Version:` beginnt bei Offset `65536 - 5` → gefunden
    - `"kein Versionsstring"` → `nullopt`
    - `"$$Version: ohne Zahl"` → `nullopt`
    - `"Vergleich"`: `{6, 492} < kMinNintendont`, `{6, 493} >= kMinNintendont`, `{7, 0} > kMinNintendont`
  - `launch_plan_test` mit `MemFs`, Fixture-Spiel `games/Metroid Prime [GM8P01]/game.iso`, Nintendont-DOL-Bild unter `sd:/apps/nintendont/boot.dol`:
    - `"GC startet mit NIN_CFG als argv[1]"`: `args.size() == 2`, `args[1].size() == 548`, Bytes `0x014..` = `/games/Metroid Prime [GM8P01]/game.iso`
    - `"GC von USB setzt USB-Bit"`: `libRoot = "usb:"` → Byte `0x00A == 0x04`
    - je ein Test pro Fehler aus der Preflight-Reihenfolge, mit erwartetem `kind` und `context`; dazu `"AHBPROT wird vor allem anderen geprüft"` (DOL fehlt **und** `ahbprotOff == false` → `AhbProt`)
    - `"NES ignoriert ahbprotOff"`
    - `"argvBlock passt"`: `packArgvBlock` liefert ok für einen 254-Byte-Pfad
  - `launch_text_test`: `NinTooOld` mit Kontext `"6.480"` → Text enthält `6.480` und `6.493`, in DE und EN
- [ ] **Step 2: Run** `ctest --preset host -R "nin_cfg|launch_plan|launch_text"`. Expected: FAIL.
- [ ] **Step 3: Implement** wie oben. `main` übergibt vorerst `ahbprotOff = true`; Task 3 ersetzt das.
- [ ] **Step 4: Run:**
  - Host-Tests komplett, `python tools/build_wii.py`
  - Expected: PASS, keine Warnungen
- [ ] **Step 5: Commit** `feat: Preflight für Nintendont`

### Task 3: AHBPROT, Verdrahtung und `argv_echo`

**Files:**
- Create: `src/platform/system.hpp/.cpp`
- Modify:
  - `src/app/main.cpp`
  - `src/core/config.hpp/.cpp`
  - `tests/host/core/config_test.cpp`
  - `tests/wii/argv_echo.c`
  - `tests/fixtures/sd/newlauncher/emulators.ini`
  - `tests/fixtures/sd/games/Metroid Prime [GM8P01]/game.iso`: Fixture prüfen; Header mit ID6 ist seit M2 vorhanden
  - `CMakeLists.txt`

**Interfaces:**
- Consumes: `preflightGame(…, ahbprotOff)` (Task 2)
- Produces:
  - `bool platform::ahbprotDisabled()`: `read32(0x0D800064) == 0xFFFFFFFF`
  - `bool Config::debugIgnoreAhbprot` aus `[debug] ignore_ahbprot`
  - `main`:
    - beim Start `NL_AHBPROT off|on`
    - `ahbprotOff = platform::ahbprotDisabled() || config.debugIgnoreAhbprot`
    - bei GC-Zielen vor dem Log-Flush `NL_NIN ver=… path=… usb=…`
    - danach derselbe Ablauf wie für Emulator-DOLs (Audio stoppen, Video herunterfahren, `chainload`)
  - `argv_echo.c`:
    - wenn `argc > 1` und die ersten 4 Byte von `argv[1]` die Magic sind: 548 Byte aus `argv[1]` kopieren
    - Felder Big-Endian lesen und `NL_ARGV_NINCFG …` im Format der Global Constraints ausgeben
    - die übrigen `NL_ARGV`-Zeilen entfallen in diesem Fall, weil die NUL-Bytes des Blocks sie sinnlos machen; `NL_ARGV 0 <dol>` bleibt
  - Fixture-`emulators.ini`: `[nintendont]` mit `dol = sd:/apps/argv_echo/boot.dol`. Die Fixture-`argv_echo` enthält keinen Versionsstring; deshalb bekommt `argv_echo.c` die Konstante `static const char kVersionTag[] __attribute__((used)) = "$$Version:6.509";`.

- [ ] **Step 1: Write the failing test** `config_test`: `ignore_ahbprot = 1` → `true`.
- [ ] **Step 2: Run**, expect FAIL.
- [ ] **Step 3: Implement** wie oben.
- [ ] **Step 4: Run:**
  - Host-Tests, Tool-Tests, `python tools/build_wii.py`
  - Smoke-Test:
    ```bash
    python tools/dolphin_smoke.py --debug ignore_ahbprot=1 --autostart GM8P01 --marker "NL_NIN ver=6.509 path=/games/Metroid Prime [GM8P01]/game.iso usb=0" --marker NL_AUDIO_STOP --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV_NINCFG ver=10 cfg=0x00000088 vid=0x00000000 lang=-1 pads=4 mc=2 id=GM8P path=/games/Metroid Prime [GM8P01]/game.iso"
    ```
  - Smoke-Test ohne Überbrückung: `python tools/dolphin_smoke.py --marker "NL_AHBPROT"`. Dabei das Ergebnis `off` oder `on` für Dolphin in der README notieren.
  - Expected: alle Marker. Fehlerfälle prüfen die Host-Tests, nicht der Smoke-Test.
- [ ] **Step 5: Commit** `feat: GameCube-Spiele über Nintendont starten`

### Task 4: Abnahme auf der Wii und README

**Files:**
- Modify:
  - `docs/hardware-acceptance.md`: neuer Abschnitt G
  - `README.md`

**Interfaces:**
- Consumes: `tools/wii_deploy.py` (M7), Ergebnis V4 aus M7 (Nintendont-Version)
- Produces:
  - Abschnitt **G – GameCube**:
    - G1 Spiel von SD
    - G2 Spiel von USB
    - G3 Speichern auf emulierter Memory Card und erneut laden
    - G4 Zwei-Disc-Spiel wechselt auf Disc 2, falls vorhanden
    - G5 Nintendont unter `sd:/apps/nintendont/boot.dol` entfernt → Dialog „Nintendont fehlt“
    - G6 Start über wiiload → Dialog „Kein Hardwarezugriff“
    - G7 GC-Controller und Wiimote+Classic im Spiel
  - README: Abschnitt „GameCube“
    - Ordnerschema `games/<Titel> [ID6]/game.iso` (+ `disc2.iso`)
    - Nintendont ab 6.493 unter `sd:/apps/nintendont/`
    - Memory-Card-Emulation an, Einstellungen von Nintendont sonst automatisch
    - Hinweis „Start nur aus dem HBC-Eintrag, nicht per wiiload“
- [ ] **Step 1: Schreiben** wie oben.
- [ ] **Step 2: Deploy** `python tools/wii_deploy.py <LW>:`. Der Nutzer arbeitet G1–G7 ab; der Agent trägt die Ergebnisse ein.
  Expected: G1–G3, G5–G7 PASS, G4 PASS oder „kein Zwei-Disc-Spiel vorhanden“.
- [ ] **Step 3: Befunde** wie M7 Task 7: Test zuerst, Fix, Commit `fix: <ID> …`, Punkt erneut prüfen.
- [ ] **Step 4: Commit** `docs: GameCube im README, Abnahme M8`

---

## Selbstreview (beim Schreiben erledigt)

- **Spec-Abdeckung:**
  - §6.5 Backend Nintendont:
    - Spielpfad: Task 1
    - Videomodus: `NIN_VID_AUTO`, Task 1
    - Controller: `MaxPads`, Task 1
    - Memcard: Task 1
    - `argv[1]`: Task 2
    - kein IOS-Reload: Task 3, gleicher Chainloader
    - Preflight DOL/ISO/AHBPROT: Task 2
  - §10 Version aus dem Binary: Task 2
  - §6.2 `disc2.iso`: G4
- **Typen:**
  - `buildNinCfg` liefert `std::array<u8, 548>`, Task 2 macht daraus `std::string(blob.begin(), blob.end())` für `LaunchPlan::args`, das `packArgvBlock` binärsicher packt (M3).
  - `NinVersion` steht in `LaunchTarget::nintendont` und in `formatNinVersion` für Marker und Dialog.
- **Offen für die Abnahme:** Ob Dolphin AHBPROT als aus meldet, klärt Task 3 Step 4; der Smoke-Test hängt davon nicht ab.
