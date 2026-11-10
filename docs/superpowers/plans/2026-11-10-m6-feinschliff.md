# M6 – Feinschliff: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Launcher spricht Deutsch und Englisch, hat eine echte Einstellungsszene, die `config.ini` schreibt, ein vollständiges HOME-Menü mit Akkustand, UI-Sounds und Hintergrundmusik von der SD.

**Architecture:** Alles Prüfbare liegt plattformfrei in `core/` bzw. in der host-testbaren `ui/`:
- Sprachtabelle als X-Makro mit `tr(Str)`
- Einstellungslogik (`stepSetting`) und `config.ini`-Schreiben
- Akkubalken
- Tonsynthese der UI-Sounds
- Playlist und Lautstärke

Auf der Wii kommen dazu:
- `platform/audio.*`: ASND, UI-Sounds auf den Stimmen 1–3
- `platform/music.*`: Musik-Thread mit Tremor, streamt in Stimme 0

Die UI meldet Sounds über `SceneRequests::sound` und Einstellungsänderungen über neue `AppCommand`s; `main` setzt sie um.

**Tech Stack:** wie M5. Neu: ASND aus libogc und Tremor (`ppc-libvorbisidec` 1.2.1, `ppc-libogg` 1.3.6 aus `dkp-libs`, am 2026-11-10 im Paketindex geprüft). Für die Fixture-Musik einmalig `ffmpeg` mit libvorbis (liegt auf dem Entwicklungsrechner).

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md`, Abschnitte:
- §3.3 (HOME-Menü), §3.4 (Einstellungen), §3.9 (Audio), §3.10 (Video)
- §4.1 (Texte DE/EN in der Sprachtabelle), §4.2 (UI übersetzt Fehlercodes über die Sprachtabelle), §4.3 (Audio-Thread)
- §5.1 (`sd:/newlauncher/music/*.ogg`), §5.4 (`config.ini`)
- §6.5 (Audio stoppen vor dem Start), §6.8, §9 M6

**Basis:** Branch `feat/m6-feinschliff` von `main` (enthält M5, Merge `5ea0ec1`).

## Global Constraints

- Alle Global Constraints aus M1–M5 gelten weiter:
  - C++20 ohne Exceptions und RTTI, `Result`
  - keine Allokation im Leerlauf (`NL_ALLOC_IDLE 0`)
  - `core/` ohne `<ogc/...>`
  - Dolphin nur mit `-u`; die private Dolphin-Konfiguration bleibt unberührt
  - keine ROMs/ISOs/WADs im Repo
  - Commits enden mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
- Sprachen: genau `De` und `En`. `language = auto` → Deutsch, wenn `CONF_GetLanguage() == CONF_LANG_GERMAN`, sonst Englisch.
- Jede Zeichenkette mit `%` hat in DE und EN dieselbe Folge von Umwandlungen (`%s`, `%u`, `%d` …).
- `config.ini` (Spec §5.4), exakte Werte beim Schreiben:
  - `language = auto|de|en`, `library = usb|sd`, `usb_timeout = <n>`
  - `mode = light|dark|auto`, `dark_from`/`dark_to` = `HH:MM`
  - `music = 0|1`, `music_volume = 0..100`, `sounds = 0|1`
  - `region = auto|EN|DE|FR|US|JA`
  - Unbekannte Schlüssel, Abschnitte (auch `[debug]`) und Kommentare bleiben erhalten. Geschrieben wird atomar über `writeAtomic`.
- Einstellungszeilen in genau dieser Reihenfolge:
  Sprache, Darstellung, Dunkel von, Dunkel bis, Musik, Lautstärke, UI-Sounds, Bibliotheksgerät, Cover-Region, „Bibliothek neu scannen“, „Fehlende Cover laden“, „VC-Injects zuordnen“.
- Schrittweiten:
  - Uhrzeit ±30 min mit Umlauf 23:30 ↔ 00:00
  - Lautstärke ±10, begrenzt auf 0..100
  - alle anderen Werte zyklisch
- Audio:
  - 48 000 Hz, 16 Bit, Big-Endian (PPC-nativ)
  - Puffer 32-Byte-ausgerichtet
  - Stimme 0 = Musik, Stimmen 1–3 = UI-Sounds reihum
  - ASND-Lautstärke = `prozent * 255 / 100`
- Musik:
  - `sd:/newlauncher/music/*.ogg` (Endung ohne Groß/klein-Unterscheidung)
  - zufällige Reihenfolge in Runden, dasselbe Stück nie zweimal hintereinander (bei ≥ 2 Stücken)
  - Ordner fehlt oder leer → keine Musik, kein Fehler
- Neue Log-Marker:
  - `NL_LANG de|en`: beim Start und bei jeder Änderung
  - `NL_CONFIG_SAVED`
  - `NL_RESCAN start`
  - `NL_AUDIO ok` / `NL_AUDIO_FAIL <code>`
  - `NL_AUDIO_STOP`
  - `NL_SOUND <name>`: nur mit `[debug] log_sounds = 1`
  - `NL_MUSIC play <datei> rate=<hz> ch=<n>`
  - `NL_MUSIC_FAIL <datei> <code>`
  - `NL_MUSIC_OFF`: Musik abgeschaltet, weil alle Stücke scheitern
  - `NL_BATTERY p1=<n> p2=<n> p3=<n> p4=<n>`: beim Öffnen des HOME-Menüs, −1 = nicht verbunden

## Review Focus

1. **Formatzeichen DE ≠ EN:** Eine übersetzte Zeichenkette mit anderer `%`-Folge lässt `snprintf` falsche Argumente lesen. Erwartet: Ein Host-Test vergleicht für jede `Str` die Folge (Task 1).
2. **`config.ini` von Hand bearbeitet:**
   - Kommentare, unbekannte Schlüssel, `[debug]`, ungültige Werte, fehlende Abschnitte, CRLF
   - Erwartet: Nach dem Speichern sind alle fremden Zeilen noch da, ungültige Werte stehen als Standardwert in der Datei, nichts wird halb geschrieben (Task 2).
3. **Kaputte Musik:** 0-Byte-`.ogg`, MP3 mit `.ogg`-Endung, Mono/Stereo, 22/32/44,1/48 kHz, nur kaputte Dateien.
   Erwartet:
   - kaputte Stücke werden übersprungen
   - nach einer ganzen Runde ohne spielbares Stück `NL_MUSIC_OFF` statt Endlosschleife und Log-Flut
   - die UI ruckelt nicht (Task 7)
4. **Start oder Beenden während Musik und Sound laufen:** Spielstart, HBC, Wii-Menü, Ausschalten. Erwartet: In jedem Weg kommt `NL_AUDIO_STOP` vor dem Chainload bzw. Reset, der Musik-Thread ist beendet, kein DSP-Brummen im Emulator (Task 7, Smoke-Marker-Reihenfolge).
5. **Grenzwerte der Einstellungen:**
   - Lautstärke 0 und 100
   - Uhrzeit-Umlauf 23:30 → 00:00
   - `dark_from == dark_to`
   - Bibliotheksgerät mitten in der Sitzung gewechselt
   - Erwartet: Werte bleiben im Bereich. Lautstärke 0 = stumm bei laufendem Thread. Das Bibliotheksgerät wirkt erst nach dem nächsten Start; die Zeile zeigt den Hinweis (Task 2, Task 3).

## Abweichungen und Festlegungen gegenüber der Spec

| Punkt | Festlegung in M6 | Grund |
|---|---|---|
| Ort der Sprachtabelle | bleibt `src/ui/strings.hpp` (Spec §4.1 nennt `core/`) | Die Datei existiert seit M1, ist host-testbar und ohne `<ogc/...>`; ein Umzug brächte nur Churn |
| UI-Sounds | zur Laufzeit synthetisiert (Sinus mit Hüllkurve), keine eingebetteten Sample-Dateien | „eigene“ Samples laut §3.9 ohne Lizenzfrage und ohne Binär-Assets im Repo |
| Bibliotheksgerät | Änderung wirkt erst nach dem nächsten Start; die Zeile zeigt „Wirkt nach Neustart“ | Umhängen von Gerät, Index und Cover-Cache zur Laufzeit lohnt für eine seltene Einstellung nicht |
| Zahnrad im Hauptbildschirm | schaltet weiter Hell → Dunkel → Automatisch, speichert jetzt aber in `config.ini` | Ein Wert statt zwei; die Einstellungsszene zeigt immer den wirksamen Wert |
| Aktionsliste aus M5 | entfällt; ihre Einträge sind Zeilen der Einstellungsszene | Spec §3.4 |
| Allokationszähler | zählt nur Allokationen des Haupt-Threads | Spec §7 verlangt 0 im Render-Loop. Tremor allokiert beim Stückwechsel im Musik-Thread |
| Bannernamen der Kanäle | Sprache folgt der UI-Sprache (Auto → Systemsprache) | eine Sprache auf dem ganzen Bildschirm |
| 16:9 / 480p (§3.10) | schon umgesetzt (`Video::init`, `Renderer`: 854 logische Breite); kein Task | Abnahme auf Hardware in M7 |

## Dateistruktur

```
src/ui/
  strings.hpp/.cpp          X-Makro NL_STRINGS, Str, Lang, tr, setLanguage, resolveLanguage, formatDate
  settings_scene.hpp/.cpp   Einstellungsszene (ersetzt actions_scene.*)
  home_menu.*               Fußleiste mit Akkustand, Ziel „Einstellungen“
  scene.hpp                 UiSound in SceneRequests, neue AppCommands, UiContext::battery
  main_scene.*, details_scene.*, keyboard.*, jump_bar.*, vc_map_scene.*, dialog_scene.*   tr() und Sounds
src/core/
  config.hpp/.cpp           formatClock, configToIniText
  settings_model.hpp/.cpp   SettingRow, stepSetting, settingEnabled
  battery.hpp               batteryBars
  ui_sound.hpp/.cpp         UiSound, ToneSpec, uiSoundSpec, uiSoundName
  synth.hpp/.cpp            synthTone
  playlist.hpp/.cpp         Playlist, asndVolume, musicFiles, shouldStopMusic
  theme.hpp/.cpp            Token danger
src/platform/
  audio.hpp/.cpp            ASND, UI-Sounds
  music.hpp/.cpp            Musik-Thread mit Tremor
  wii_input.*               Akkustand
src/app/main.cpp, selftest.*        Verdrahtung, Selbsttest
tests/fixtures/sd/newlauncher/music/tone.ogg   3 s Sinus, selbst erzeugt
CMakeLists.txt, .github/workflows/build.yml    vorbisidec, ogg
```

---

### Task 1: Sprachtabelle DE/EN

**Files:**
- Modify:
  - `src/ui/strings.hpp` (+ neu `src/ui/strings.cpp`)
  - `src/ui/layout.cpp` (Datum)
  - `src/ui/launch_text.cpp`
  - alle Nutzer von `kStr…` in `src/ui/*.cpp` und `src/app/*.cpp`
  - `tests/fixtures/sd/newlauncher/config.ini`: `language = de`
- Test:
  - `tests/host/ui/strings_test.cpp`
  - `tests/host/ui/layout_test.cpp`

**Interfaces:**
- Produces:
  - `#define NL_STRINGS(X)`: Einträge `X(Id, "deutsch", "english")`. Jeder heutige `kStrFoo` wird zu `Str::Foo` mit unverändertem deutschem Text.
  - `enum class Str : u16 { …, Count }`
  - `enum class Lang : u8 { De, En }`
  - `const char* tr(Str s)`: Text in der aktiven Sprache, nie `nullptr`
  - `void setLanguage(Lang)`, `Lang language()`
  - `Lang resolveLanguage(Config::Language setting, bool systemGerman)`
  - `const char* langKey(Lang)`: `"de"` / `"en"`
  - `const char* weekdayName(int weekday0Mon)`, `const char* monthName(int month1)`
  - Datumsformat (bestehende Funktion in `layout.cpp`):
    - DE unverändert `"%s, %d. %s %d"`: „Montag, 10. November 2026“
    - EN `"%s, %s %d, %d"`: „Monday, November 10, 2026“

- [ ] **Step 1: Write the failing tests** in `strings_test.cpp`:
  - `TEST_CASE("jede Str hat in DE und EN nicht-leeren Text")`
  - `TEST_CASE("Formatfolge DE == EN")`: Hilfsfunktion im Test extrahiert alle Umwandlungen (`%` + Flags + Typ, `%%` ausgenommen); für alle `Str` gleich.
  - `TEST_CASE("setLanguage schaltet tr um")`: `setLanguage(Lang::En); CHECK(std::string(tr(Str::Settings)) == "Settings");` dann `Lang::De` → `"Einstellungen"`.
  - `TEST_CASE("resolveLanguage")`:
    - `(Auto, true)` → `De`, `(Auto, false)` → `En`
    - `(De, false)` → `De`, `(En, true)` → `En`
  - `layout_test`: Datum EN `"Monday, November 10, 2026"` für (weekday 0, 10, 11, 2026); DE `"Montag, 10. November 2026"`.
- [ ] **Step 2: Run** `ctest --preset host -R strings`. Expected: FAIL (`Str`/`tr` unbekannt).
- [ ] **Step 3: Implement**:
  - Tabelle schreiben, `kStr…` ersetzen und Wochentage/Monate als zwei Arrays je Sprache.
  - Statische Arrays mit `kStr…`-Zeigern (z. B. `kLabels` in `home_menu.cpp`) werden zu `Str`-Arrays, aufgelöst beim Zeichnen.
  - Fixture-`config.ini` auf `language = de`, damit Selbsttest und Smoke unabhängig von Dolphins Systemsprache bleiben.
- [ ] **Step 4: Run** Host-Tests komplett und `python tools/build_wii.py`. Expected: PASS und ein Build ohne Warnungen.
- [ ] **Step 5: Commit** `feat: Sprachtabelle DE/EN mit tr()`

### Task 2: `config.ini` schreiben und Einstellungslogik

**Files:**
- Modify: `src/core/config.hpp/.cpp`
- Create: `src/core/settings_model.hpp/.cpp`
- Test:
  - `tests/host/core/config_test.cpp`
  - `tests/host/core/settings_model_test.cpp`

**Interfaces:**
- Consumes: `IniDoc::parse/set/serialize`, `parseClock`, `Config`
- Produces:
  - `std::string formatClock(int minuteOfDay)`: `1140` → `"19:00"`, `0` → `"00:00"`
  - `std::string configToIniText(const Config& c, std::string_view previousText)`: `previousText` parsen, alle Schlüssel aus Spec §5.4 setzen, serialisieren.
  - `enum class SettingRow : u8 { Language, Theme, DarkFrom, DarkTo, Music, Volume, Sounds, Library, CoverRegion, Rescan, FetchCovers, VcMap, Count }`
  - `bool isAction(SettingRow r)`: `true` für Rescan, FetchCovers, VcMap
  - `void stepSetting(Config& c, SettingRow r, int dir)`: `dir` ist ±1; Aktionszeilen bleiben unverändert.
    - Language: Auto → De → En → Auto
    - Theme: Light → Dark → Auto → Light
    - DarkFrom/DarkTo: ±30 min mit Umlauf
    - Music und Sounds: umschalten
    - Volume: ±10, begrenzt auf 0..100
    - Library: Usb ↔ Sd
    - CoverRegion: `auto` → `EN` → `DE` → `FR` → `US` → `JA` → `auto`
  - `bool settingEnabled(const Config& c, SettingRow r)`:
    - DarkFrom/DarkTo nur bei `ThemeMode::Auto`
    - Volume nur bei `music`
    - alles andere `true`

- [ ] **Step 1: Write the failing tests:**
  - `config_test`:
    - `"configToIniText erhält [debug] und Kommentare"`: Eingabe mit `; Kommentar`, `[debug]\nautostart = nes/x.nes` und unbekanntem `foo = 1` → alle drei Zeilen stehen in der Ausgabe.
    - `"Round-Trip"`: `Config` mit `language=En`, `themeMode=Auto`, `darkFromMin=19*60+30`, `music=false`, `musicVolume=40`, `sounds=false`, `library=Sd`, `coverRegion="FR"` → `configFromIni(IniDoc::parse(text))` liefert dieselben Werte; der Text enthält `language = en`, `dark_from = 19:30`, `music = 0`, `region = FR`.
    - `"ungültiger Wert wird als Standard geschrieben"`: `music_volume = 300` → `music_volume = 60`.
    - `formatClock(0) == "00:00"`, `formatClock(1410) == "23:30"`.
  - `settings_model_test`:
    - Zyklus Language über 3 Schritte zurück auf `Auto`
    - Volume bei 100 mit `+1` bleibt 100, bei 0 mit `-1` bleibt 0
    - DarkTo 23:30 mit `+1` → 0
    - DarkFrom 0 mit `-1` → 1410
    - CoverRegion `JA` mit `+1` → `auto`, `auto` mit `-1` → `JA`
    - `settingEnabled(DarkFrom)` nur bei `ThemeMode::Auto`
    - `stepSetting(Rescan, +1)` ändert nichts
- [ ] **Step 2: Run** `ctest --preset host -R "config|settings_model"`. Expected: FAIL.
- [ ] **Step 3: Implement** wie oben. `configToIniText` schreibt in die bestehenden Abschnitte `[general]`, `[theme]`, `[audio]`, `[covers]`.
- [ ] **Step 4: Run** dieselben Tests. Expected: PASS.
- [ ] **Step 5: Commit** `feat: config.ini schreiben, Einstellungslogik`

### Task 3: Einstellungsszene und HOME-Anbindung

**Files:**
- Create:
  - `src/ui/settings_scene.hpp/.cpp`
  - Test: `tests/host/ui/settings_scene_test.cpp`
- Delete: `src/ui/actions_scene.hpp/.cpp`, `tests/host/ui/actions_scene_test.cpp`
- Modify:
  - `src/ui/scene.hpp`
  - `src/ui/home_menu.*`
  - `src/ui/strings.hpp`
  - `tests/host/ui/home_menu_test.cpp`
  - `CMakeLists.txt`

**Interfaces:**
- Consumes: `SettingRow`, `stepSetting`, `settingEnabled`, `isAction` (Task 2); `tr`, `Str` (Task 1)
- Produces:
  - `AppCommand::SettingsChanged`, `AppCommand::SaveSettings`, `AppCommand::Rescan`; Namen `settings_changed`, `save_settings`, `rescan`
  - `class SettingsScene final : public Scene`:
    - `void setConfig(Config* live)`: die Szene ändert `*live` direkt
    - `onEnter()`: merkt sich eine Kopie zum Vergleich und setzt den Fokus auf Zeile 0
    - `int focus() const`, `gfx::Rect rowRect(const UiContext&, int row) const`
    - `isOverlay() == true`
  - Bedienung:
    - Steuerkreuz hoch/runter bzw. Zeiger: Fokus
    - links/rechts: `stepSetting(±1)`
    - A auf einer Werte-Zeile: `stepSetting(+1)`
    - Jede Änderung → `out.command = SettingsChanged`
    - Ausgegraute Zeilen nehmen keinen Schritt an, der Fokus überspringt sie aber nicht.
    - A auf einer Aktionszeile → `out.pop = true`, dazu `Rescan`, `FetchCovers` bzw. `OpenVcMap`. War vorher etwas geändert, kommt `SaveSettings` vorher in einem eigenen Frame (siehe unten).
    - B → `out.pop = true`; dazu `SaveSettings`, wenn `*live` von der Kopie abweicht.
    - Zeilenhöhe 30, Karte mittig. Zeile „Bibliotheksgerät“ zeigt zusätzlich `Str::TakesEffectAfterRestart`, wenn sich der Wert vom Stand bei `onEnter` unterscheidet.
  - Eine Anforderung trägt nur einen Befehl. A auf einer Aktionszeile mit ungespeicherten Änderungen meldet deshalb in diesem Frame `SaveSettings` (ohne `pop`) und im nächsten `update` die Aktion mit `pop`.
  - `HomeMenu::setSettings(Scene*)` ersetzt `setActions`; die Kachel „Einstellungen“ ergibt `pop` + `push(settings)`.

- [ ] **Step 1: Write the failing tests** in `settings_scene_test.cpp`; Hilfsfunktion `press(Action)` wie in `home_menu_test`:
  - `"rechts auf Sprache ändert Config und meldet SettingsChanged"`: `language` Auto → De, `out.command == SettingsChanged`, kein `pop`.
  - `"B ohne Änderung schließt ohne SaveSettings"`
  - `"B nach Änderung meldet SaveSettings"`
  - `"Lautstärke ausgegraut bei Musik aus"`: Musik aus → Fokus auf Lautstärke → rechts → `musicVolume` unverändert.
  - `"A auf VC-Injects zuordnen"` (Zeile 11) ohne Änderungen → `pop` + `OpenVcMap`.
  - `"A auf Bibliothek neu scannen nach Änderung"`:
    - erster Frame `SaveSettings` ohne `pop`
    - zweiter Frame `pop` + `Rescan`
  - `"Zeiger auf Zeile 3 setzt Fokus 3"` über `rowRect`
  - `home_menu_test`: Kachel 2 ergibt `pop` + `push` der Einstellungsszene.
- [ ] **Step 2: Run** `ctest --preset host -R "settings_scene|home_menu"`. Expected: FAIL.
- [ ] **Step 3: Implement** wie oben. Werte-Texte über `tr` (z. B. `Str::On`/`Str::Off`, `Str::ThemeLight` …), Uhrzeit über `formatClock`, Lautstärke als `"%d %%"`.
- [ ] **Step 4: Run** Host-Tests komplett. Expected: PASS.
- [ ] **Step 5: Commit** `feat: Einstellungsszene statt Aktionsliste`

### Task 4: HOME-Fußleiste mit Akkustand

**Files:**
- Create:
  - `src/core/battery.hpp`
  - Test: `tests/host/core/battery_test.cpp`
- Modify:
  - `src/core/theme.hpp/.cpp`: Token `danger`
  - `src/ui/scene.hpp`: `UiContext::battery`
  - `src/ui/home_menu.cpp`
  - `src/platform/wii_input.hpp/.cpp`
  - `tests/host/core/theme_test.cpp`

**Interfaces:**
- Produces:
  - `constexpr int batteryBars(u8 raw)`: `std::min(4, (raw * 4 + 199) / 200)`. `raw` ist `WPADData::battery_level`; 200 (0xC8) gilt als voll.
  - `std::array<s8, 4> UiContext::battery{-1, -1, -1, -1}`: −1 = nicht verbunden, sonst 0..4.
  - `std::array<s8, 4> platform::WiiInput::battery() const`: aktualisiert in `poll()` alle 60 Frames. Verbunden = `WPAD_Probe(chan, &type) == WPAD_ERR_NONE`.
  - `Theme::danger`: hell `#D93025`, dunkel `#F28B82`
  - Fußleiste: links `P1`…`P4` mit vier Balken. Bei ≤ 1 Balken in `danger`, nicht verbunden als `P<n> –` in `textMuted`. Rechts weiter `Str::HomeCloses`.
  - `HomeMenu::onEnter` loggt `NL_BATTERY …` über einen Callback-freien Weg: `main` loggt beim `push` des HOME-Menüs mit `ctx.battery`.

- [ ] **Step 1: Write the failing tests:**
  - `battery_test`: `batteryBars(0) == 0`, `(1) == 1`, `(50) == 1`, `(51) == 2`, `(150) == 3`, `(151) == 4`, `(200) == 4`, `(255) == 4`
  - `theme_test`: `danger` hat in hell und dunkel einen Kontrast ≥ 3 zu `bar` (bestehende Kontrast-Hilfe im Test).
- [ ] **Step 2: Run**, expect FAIL.
- [ ] **Step 3: Implement** wie oben.
- [ ] **Step 4: Run** Host-Tests und `python tools/build_wii.py`. Expected: PASS, keine Warnungen.
- [ ] **Step 5: Commit** `feat: Akkustand P1–P4 im HOME-Menü`

### Task 5: Einstellungen in `main` verdrahten

**Files:**
- Modify:
  - `src/app/main.cpp`
  - `src/platform/nand_titles.hpp/.cpp` (Bannersprache)

**Interfaces:**
- Consumes: `SettingsScene`, `AppCommand::SettingsChanged/SaveSettings/Rescan` (Task 3); `configToIniText` (Task 2); `resolveLanguage`, `setLanguage`, `langKey` (Task 1)
- Produces in `main`:
  - Start: `setLanguage(resolveLanguage(config.language, CONF_GetLanguage() == CONF_LANG_GERMAN))`, danach `NL_LANG <key>`.
  - `SettingsChanged`:
    - Sprache neu auflösen, `NL_LANG` nur bei Wechsel
    - `ctx.themeMode = config.themeMode` und `theme.setTarget(…, true)`
    - Audio-Einstellungen weiterreichen; bis Task 6/7 ist das ein No-op
  - `SaveSettings` und `CycleTheme`:
    - `config.themeMode` übernehmen
    - `writeText(fs, kConfigPath, configToIniText(config, <zuletzt gelesener oder geschriebener Text>))`
    - bei Erfolg `NL_CONFIG_SAVED`, sonst `NL_WARN config.ini nicht gespeichert`
  - `Rescan`:
    - Läuft schon ein Scan, nichts tun.
    - Sonst `worker.start(fs, libRoot, nullptr, &meta)`, Toast `Str::Rescanning`, `NL_RESCAN start`.
    - Das Ergebnis läuft über den bestehenden Scan-Pfad.
  - `platform::systemBannerLang()` bekommt `BannerLang bannerLangFor(ui::Lang)` als Ersatz beim Laden der Kanalnamen: De → German, En → English.

- [ ] **Step 1: Implement** wie oben. Logik, die nicht in `main` muss, steckt schon in Task 1–3 und ist dort getestet.
- [ ] **Step 2: Run:**
  - `python tools/build_wii.py`
  - `python tools/dolphin_smoke.py --marker "NL_LANG de" --marker NL_READY`
  - Expected: Build ohne Warnungen, beide Marker.
- [ ] **Step 3: Commit** `feat: Einstellungen wirken sofort und werden gespeichert`

### Task 6: UI-Sounds

**Files:**
- Create:
  - `src/core/ui_sound.hpp/.cpp`, `src/core/synth.hpp/.cpp`
  - `src/platform/audio.hpp/.cpp`
  - Test: `tests/host/core/synth_test.cpp`
- Modify:
  - `src/ui/scene.hpp`: `SceneRequests::sound`
  - `main_scene.cpp`, `details_scene.cpp`, `home_menu.cpp`, `settings_scene.cpp`, `keyboard.cpp`, `jump_bar.cpp`, `vc_map_scene.cpp`, `dialog_scene.cpp`
  - ihre Tests (`main_scene_test`, `home_menu_test`, `settings_scene_test`, `details_scene_test`, `keyboard_test`)
  - `src/app/main.cpp`, `src/core/config.*` (`debugLogSounds`), `CMakeLists.txt` (asnd)

**Interfaces:**
- Produces:
  - `enum class UiSound : u8 { None, Move, Confirm, Back, Open, Error }`
  - `const char* uiSoundName(UiSound)`: `move`, `confirm`, `back`, `open`, `error`
  - `struct ToneSpec { float startHz, endHz; u16 ms; float amplitude; }`
  - `ToneSpec uiSoundSpec(UiSound)`, Werte fest:

    | Sound | Frequenz | Dauer | Amplitude |
    |---|---|---|---|
    | Move | 1200 → 1200 Hz | 25 ms | 0,20 |
    | Confirm | 880 → 1320 Hz | 70 ms | 0,30 |
    | Back | 660 → 440 Hz | 70 ms | 0,30 |
    | Open | 523 → 784 Hz | 110 ms | 0,25 |
    | Error | 220 → 196 Hz | 160 ms | 0,35 |

  - `std::vector<s16> synthTone(const ToneSpec&, u32 sampleRate)`:
    - Sinus mit linearem Frequenzverlauf (Phase akkumuliert)
    - 5 ms linearer Anstieg, danach linearer Abfall auf 0 bis zum Ende
    - Länge `sampleRate * ms / 1000`, aufgerundet auf ein Vielfaches von 16 Samples (32 Byte), mit Nullen aufgefüllt
  - `UiSound SceneRequests::sound = UiSound::None`. Regel für alle Szenen:
    - Fokuswechsel (Taste oder Zeiger) → `Move`
    - A mit Wirkung → `Confirm`
    - B schließt → `Back`
    - Overlay öffnen (HOME, Details, Suche, Einstellungen) → `Open`
  - `class platform::Audio`:
    - `bool init()`: `ASND_Init`, `ASND_Pause(0)`; synthetisiert alle Sounds in 32-Byte-ausgerichtete Puffer (`memalign`); `NL_AUDIO ok` bzw. `NL_AUDIO_FAIL`
    - `void play(UiSound)`: Stimmen 1–3 reihum mit `ASND_SetVoice(v, VOICE_MONO_16BIT, 48000, 0, buf, bytes, 255, 255, nullptr)`; No-op bei `None` oder abgeschalteten Sounds
    - `void setSounds(bool on)`
    - `void shutdown()`: alle Stimmen stoppen, `ASND_End`, `NL_AUDIO_STOP`; mehrfach aufrufbar
  - `main`:
    - spielt `req.sound` nach `scenes.update`
    - spielt `Error`, wenn ein Fehlerdialog geöffnet wird
    - loggt `NL_SOUND <name>` nur mit `[debug] log_sounds = 1` (`Config::debugLogSounds`)

- [ ] **Step 1: Write the failing tests:**
  - `synth_test`:
    - Länge für Move bei 48 000 Hz = 1200 aufgerundet auf 1200, für Open 5280
    - erstes und letztes Sample == 0
    - `max(|s|) <= 0.35 * 32767 + 1`
    - Confirm hat in der zweiten Hälfte mehr Nulldurchgänge pro Sample als in der ersten
  - Szenen-Tests:
    - `main_scene_test`: rechts → `Move`, A auf Cover → `Confirm`
    - `home_menu_test`: B → `Back`
    - `settings_scene_test`: Wert ändern → `Confirm`, Fokus runter → `Move`
    - `details_scene_test`: B → `Back`
    - `keyboard_test`: A auf Taste → `Confirm`
- [ ] **Step 2: Run**, expect FAIL.
- [ ] **Step 3: Implement** wie oben.
- [ ] **Step 4: Run:**
  - Host-Tests, `python tools/build_wii.py`
  - Smoke-Test `--marker "NL_AUDIO ok" --marker NL_READY`
  - Expected: PASS, keine Warnungen, beide Marker.
- [ ] **Step 5: Commit** `feat: UI-Sounds über ASND`

### Task 7: Hintergrundmusik mit Tremor

**Files:**
- Create:
  - `src/core/playlist.hpp/.cpp`
  - `src/platform/music.hpp/.cpp`
  - `tests/fixtures/sd/newlauncher/music/tone.ogg`
  - Test: `tests/host/core/playlist_test.cpp`
- Modify:
  - `src/platform/audio.*`
  - `src/app/main.cpp`
  - `CMakeLists.txt` (`vorbisidec`, `ogg` aus den Portlibs)
  - `.github/workflows/build.yml`
  - `tools/make_fixtures.py` (Kommentar zur Herkunft von `tone.ogg`)

**Interfaces:**
- Consumes: `FileSystem::list`; `Audio` (Task 6); `asndVolume` (unten)
- Produces:
  - `class Playlist`:
    - `Playlist(std::vector<std::string> files, u32 seed)`
    - `bool empty() const`, `std::size_t size() const`
    - `const std::string& next()`
    - Mischen mit eigenem xorshift32 (`seed == 0` → 1), damit Host und Wii gleich mischen.
    - Jede Runde enthält jedes Stück genau einmal; erstes Stück einer Runde ≠ letztes der vorigen bei `size() >= 2`.
  - `int asndVolume(int percent)`: begrenzt auf 0..100, dann `percent * 255 / 100`
  - `std::vector<std::string> musicFiles(FileSystem&, std::string_view dir)`:
    - volle Pfade aller Dateien mit Endung `.ogg` (Groß/klein egal), sortiert
    - Ordner werden übergangen
    - fehlender Ordner → leer
  - `bool shouldStopMusic(u32 consecutiveFailures, std::size_t trackCount)`: `trackCount == 0 || consecutiveFailures >= trackCount`
  - `class platform::Music`:
    - `bool start(std::vector<std::string> files, u32 seed)`: leer → `false`, kein Thread
    - `void setEnabled(bool)`, `void setVolume(int percent)`
    - `void stop()`: Thread beenden und joinen, Stimme 0 stoppen
  - Thread:
    - `ov_open_callbacks` über stdio
    - Ausgabe in 4 Puffer zu 8192 Byte (32-Byte-ausgerichtet); `ASND_SetVoice` für den ersten Puffer, danach `ASND_AddVoice`, sobald `ASND_TestVoiceBufferReady(0)`
    - Format `VOICE_MONO_16BIT` oder `VOICE_STEREO_16BIT` nach `ov_info`, Rate aus `ov_info`
    - bei Ende nächstes Stück, bei Fehler `NL_MUSIC_FAIL` und nächstes Stück
    - `shouldStopMusic` → `NL_MUSIC_OFF`, Thread endet
    - Log-Zeilen gehen wie beim Netz-Thread über eine Mutex-geschützte Liste an den Haupt-Thread.
    - Bei abgeschalteter Musik Stimme 0 stoppen und alle 50 ms auf Wiedereinschalten prüfen; Lautstärke über `ASND_ChangeVolumeVoice(0, v, v)`
  - `main`:
    - nach `NL_READY` `music.start(musicFiles(fs, "sd:/newlauncher/music"), ticks)`, sofern `config.music`
    - `SettingsChanged` → `setEnabled`, `setVolume`
    - vor jedem Chainload, Kanalstart, HBC-Exit, Wii-Menü und Ausschalten in dieser Reihenfolge: Verlauf speichern (nur Spielstart) → `music.stop()` → `audio.shutdown()` (`NL_AUDIO_STOP`) → Video herunterfahren
  - `tone.ogg`, einmalig erzeugt mit
    `ffmpeg -f lavfi -i "sine=frequency=440:duration=3" -ac 1 -ar 32000 -c:a libvorbis -q:a 0 tone.ogg`
    (unter 20 KB)
  - CI: `test -f "$DEVKITPRO/portlibs/ppc/lib/libvorbisidec.a" || dkp-pacman -S --noconfirm --needed ppc-libvorbisidec ppc-libogg`

- [ ] **Step 1: Write the failing tests** in `playlist_test.cpp`:
  - `"jede Runde enthält jedes Stück einmal"`: 5 Dateien, 3 Runden, je Runde sortiert == Eingabe
  - `"kein Stück zweimal hintereinander"`: 2 Dateien, 200 `next()`, nie zwei gleiche nacheinander
  - `"ein Stück wiederholt sich"`: 1 Datei, 3 × `next()` gleich
  - `"gleicher seed, gleiche Folge"`
  - `"musicFiles filtert .ogg"`: `MemFs` mit `a.OGG`, `b.ogg`, `c.mp3`, Ordner `x.ogg` → `{".../a.OGG", ".../b.ogg"}`; fehlender Ordner → leer
  - `asndVolume`: `(60) == 153`, `(0) == 0`, `(100) == 255`, `(150) == 255`, `(-5) == 0`
  - `shouldStopMusic`: `(0, 0) == true`, `(2, 3) == false`, `(3, 3) == true`
- [ ] **Step 2: Run**, expect FAIL.
- [ ] **Step 3: Implement** wie oben. Pakete lokal installieren:
  ```bash
  /c/devkitPro/msys2/usr/bin/pacman -S --noconfirm --needed ppc-libvorbisidec ppc-libogg
  ```
- [ ] **Step 4: Run:**
  - Host-Tests und `python tools/build_wii.py`
  - `python tools/dolphin_smoke.py --marker NL_READY --marker "NL_MUSIC play sd:/newlauncher/music/tone.ogg rate=32000 ch=1"`
  - `python tools/dolphin_smoke.py --autostart "nes/Super Mario Bros. (World).nes" --marker NL_AUDIO_STOP --marker "ARGV 1"`
  - Expected: alle Marker. `NL_AUDIO_STOP` steht im Log vor dem ersten `ARGV`-Marker.
- [ ] **Step 5: Commit** `feat: Hintergrundmusik von SD mit Tremor`

### Task 8: Selbsttest, Abnahme und README

**Files:**
- Modify:
  - `src/app/selftest.hpp/.cpp`
  - `src/app/main.cpp`
  - `README.md`
  - `tools/dolphin_smoke.py` (nur falls neue Optionen nötig)

**Interfaces:**
- Consumes: alles aus Task 1–7
- Produces:
  - `SelftestView`: `actions` → `const ui::SettingsScene* settings`, neu `const Config* config`
  - Ablauf nach dem bisherigen HOME-Teil:
    - HOME → „Einstellungen“ → Fokus auf Sprache → rechts (De → En): `NL_ASSERT(ui::language() == ui::Lang::En)`
    - links zurück auf De
    - Fokus auf Lautstärke → links (60 → 50)
    - B → `NL_CONFIG_SAVED` im Log, `NL_ASSERT(v.config->musicVolume == 50)`
    - Danach öffnet der VC-Zuordnungsteil die Szene über HOME → „Einstellungen“ → Zeile 11 → A statt über die Aktionsliste.
  - Allokationszähler: zählt nur, wenn `LWP_GetSelf()` dem beim Start gemerkten Haupt-Thread entspricht.
  - Leerlauf-Fenster `kIdleStart`/`kIdleEnd` liegt hinter allen Eingaben; sonst beide gleich weit verschieben.

- [ ] **Step 1: Selbsttest anpassen** wie oben.
- [ ] **Step 2: Abnahme**, jede mit Expected PASS bzw. allen Markern:
  1. Host-Tests
  2. Tool-Tests
  3. Wii-Build ohne Warnungen
  4. Smoke-Tests aus Task 5, 6 und 7
  5. Selbsttest mit allen bisherigen Markern plus `NL_LANG en`, `NL_CONFIG_SAVED`, `NL_ALLOC_IDLE 0`
  6. Cover-Smoke-Tests aus M5 (Regression)
- [ ] **Step 3: README:**
  - Abschnitt „Einstellungen“ mit der Tabelle aus Spec §3.4 und den Bedienhinweisen
  - Abschnitt „Musik“: Ordner, Format Ogg Vorbis, zufällige Reihenfolge, Lautstärke
  - Sprachen
  - neue Marker und `[debug] log_sounds`
  - Satz zur Aktionsliste entfernen
- [ ] **Step 4: Commit** `docs: M6 im README, Selbsttest mit Einstellungen`

---

## Selbstreview (beim Schreiben erledigt)

- **Spec-Abdeckung:**
  - §3.3: Task 4; Kacheln und „HOME schließt“ gibt es seit M1
  - §3.4: Task 2, 3, 5
  - §3.9: Task 6, 7
  - §3.10: schon umgesetzt, Abnahme M7
  - §4.2 Sprachtabelle für Fehler: Task 1 (`launch_text.cpp`)
  - §4.3 Audio-Thread: Task 7
  - §6.5 Audio stoppen: Task 7
- **Typen:**
  - `SettingRow` und `stepSetting` (Task 2) werden in Task 3 genutzt
  - `UiSound` (Task 6) steht in `core/ui_sound.hpp`, damit `ui/scene.hpp` ihn ohne Plattform kennt
  - `asndVolume` (Task 7) wird nur in `platform/music.cpp` und `audio.cpp` benutzt
