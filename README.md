# newlauncher

Hochperformanter Wii-Launcher im Wii-Stil für Homebrew/cIOS-Setups: Wii-Spiele nativ, GameCube über Nintendont, N64 über VC-Inject/Not64/Wii64, NES/SNES/GB/GBC/GBA über Emulatoren – sortiert nach Konsolengeneration.

- Design: [docs/superpowers/specs/2026-11-09-wii-launcher-design.md](docs/superpowers/specs/2026-11-09-wii-launcher-design.md)
- Pläne: [docs/superpowers/plans/](docs/superpowers/plans/)
- Lizenz: GPLv3 ([LICENSE](LICENSE))

## Voraussetzungen (Windows)

- **devkitPro** mit Komponente „Wii Development“ (`C:\devkitPro`), danach in dessen MSYS2-Shell:
  `pacman -S --needed wii-dev cmake ppc-freetype`
- **Host-Compiler:** `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`
- **Ninja:** `winget install --id Ninja-build.Ninja -e`
- **CMake** ≥ 3.25 und **Python 3**
- **Dolphin** unter `../wii_tools/Dolphin-x64/` (neben diesem Repo)

## Befehle

Host-Unit-Tests (plattformfreier Kern):
```bash
cmake --preset host && cmake --build --preset host && ctest --preset host
```

Werkzeug-Tests:
```bash
python -m unittest discover -s tools/tests -v
```

Wii-Build (`build/wii/newlauncher.dol` und Selbsttest `build/wii/nl_selftest.dol`):
```bash
python tools/build_wii.py
```

DOL in Dolphin starten (eigener Dev-User-Ordner, private Dolphin-Konfiguration bleibt unberührt):
```bash
../wii_tools/Dolphin-x64/Dolphin.exe -u ../wii_tools/dolphin-user -e build/wii/newlauncher.dol
```

Auf die virtuelle SD deployen (Launcher + Fixture-Bibliothek aus `tests/fixtures/sd/`) bzw. Smoke-Test (wartet auf `NL_READY` im Dolphin-Log; `--no-fixtures` lässt die SD wie sie ist):
```bash
python tools/deploy_dolphin.py
python tools/dolphin_smoke.py
```

Selbsttest in Dolphin (geskriptete Eingaben, keine Allokation im Leerlauf, absichtlicher Absturz am Ende):
```bash
python tools/dolphin_smoke.py --dol build/wii/nl_selftest.dol --marker "NL_LIBRARY games=12" --marker NL_FAV_SAVED --marker "NL_SEARCH mario=5" --marker "NL_PREFLIGHT_FAIL Unsupported n64/" --marker "NL_ALLOC_IDLE 0" --marker NL_SELFTEST_OK --marker "NL_CRASH id=7"
```

Chainload-Test in Dolphin (startet über `[debug] autostart` ein NES-Spiel der Fixtures; die Test-DOL `argv_echo` meldet die übergebenen Argumente):
```bash
python tools/dolphin_smoke.py --autostart "nes/Zelda (Europe) (Rev 1).nes" --marker "NL_LAUNCH nes/Zelda (Europe) (Rev 1).nes" --marker "NL_ARGV argc=3" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/nes/" --marker "NL_ARGV 2 Zelda (Europe) (Rev 1).nes"
```

Die CI baut `newlauncher.dol`, `nl_selftest.dol` und `argv_echo.dol` und lädt sie als Artefakte hoch.

Schriften neu reduzieren (nur bei Schriftwechsel, braucht `pip install fonttools`):
```bash
python tools/subset_fonts.py MPLUSRounded1c-Regular.ttf MPLUSRounded1c-Medium.ttf
```

Fixture-Bibliothek neu erzeugen (nur Dummy-Header und leere ROM-Dateien, keine Spiele):
```bash
python tools/make_fixtures.py tests/fixtures/sd
```

## Dateien auf SD und Bibliotheksgerät

`<lib>` ist `usb` oder `sd` (Einstellung `library` in `config.ini`).

```
sd:/apps/newlauncher/        boot.dol, meta.xml
sd:/newlauncher/config.ini   Einstellungen (wird beim ersten Start mit Standardwerten angelegt)
sd:/newlauncher/emulators.ini   Emulator-Pfade und Argumente (wird beim ersten Start angelegt)
sd:/newlauncher/favorites.ini, history.ini, log.txt
<lib>:/newlauncher/cache/index.bin   Spiele-Index (vom Launcher geschrieben)
<lib>:/newlauncher/cache/meta.bin    Metadaten (PC-Skript, ab M5)
<lib>:/wbfs/Titel [ID6]/ID6.wbfs|.iso    Wii
<lib>:/games/Titel [ID6]/game.iso        GameCube
<lib>:/roms/nes|snes|gb|gbc|gba|n64/     ROMs
```

## Emulatoren

NES, SNES, GB, GBC und GBA starten über Emulator-DOLs von der SD. Der Launcher lädt die DOL, übergibt die ROM als `argv` und springt per Chainloader hinein. `sd:/newlauncher/emulators.ini` legt je Konsole die DOL und das Argument-Schema fest. Fehlt die Datei, wird sie mit diesen Standardwerten angelegt:

| Konsole | DOL | `args` |
|---|---|---|
| `nes` | `sd:/apps/fceugx/boot.dol` | `{dir}\|{file}` |
| `snes` | `sd:/apps/snes9xgx/boot.dol` | `{dir}\|{file}` |
| `gb`, `gbc`, `gba` | `sd:/apps/vbagx/boot.dol` | `{dir}\|{file}` |

`args` ist eine durch `|` getrennte Liste; jeder Eintrag wird ein `argv`-Element hinter `argv[0]` (immer der DOL-Pfad). Platzhalter:
- `{dir}`: Ordner der ROM mit abschließendem `/`
- `{file}`: Dateiname
- `{path}`: vollständiger Pfad

Fehlende Einträge fallen auf den Standard zurück. N64 (Not64/Wii64) folgt mit M4. Bis dahin zeigt „Starten“ bei N64, GameCube und Wii einen Hinweis.

Vor dem Start prüft der Launcher, ob die DOL existiert und gültig ist und ob die ROM vorhanden ist. Fehlt etwas, erscheint ein Dialog, und der Launcher läuft weiter.

## Bedienung

| Aktion | Wiimote | Nunchuk | GameCube-Controller |
|---|---|---|---|
| Navigieren | Steuerkreuz | Stick | Stick, Steuerkreuz |
| Zeigen | IR-Zeiger | – | – |
| Bestätigen | A | – | A |
| Zurück | B | – | B |
| Konsole wechseln | − / + | – | L / R |
| Favorit umschalten | 1 | – | Y |
| Suche / A–Z | 2 | – | X |
| HOME-Menü | HOME | – | Start |
| Debug-Overlay | 1 + 2 | – | Z + Start |

- **A–Z:** Taste „Suche / A–Z“ öffnet die Sprungleiste; ein Buchstabe springt zum ersten Titel (ohne Treffer zum nächsten Buchstaben), „Suche“ öffnet die Bildschirmtastatur.
- **Suche:** filtert live über alle Konsolen, ohne Rücksicht auf Groß/klein, Akzente und No-Intro-Tags; B beendet den Suchmodus.
- **Details:** A auf einem Spiel; „Starten“ startet den Emulator (siehe [Emulatoren](#emulatoren)), Favorit per Knopf oder Taste 1.

Die Darstellung kommt aus `config.ini` (Standard: Hell, „Automatisch“ dunkel von 19 bis 7 Uhr); das Zahnrad unten rechts schaltet sie für die laufende Sitzung um (Hell → Dunkel → Automatisch), Einstellungen folgen mit M6.

## Log-Marker (Dolphin-Log, OSREPORT)

| Marker | Bedeutung |
|---|---|
| `NL_READY` | erster Frame gezeichnet |
| `NL_ATLAS ok` | Schrift-Atlas aufgebaut |
| `NL_ERROR <text>` | Start abgebrochen |
| `NL_CMD <name>` | Befehl aus dem HOME-Menü bzw. Zahnrad (`exit_hbc`, `wii_menu`, `power_off`, `cycle_theme`) |
| `NL_ALLOC_IDLE <n>` | Selbsttest: Allokationen in 120 Leerlauf-Frames |
| `NL_SELFTEST_OK` | Selbsttest bestanden |
| `NL_PANIC <ausdruck> <datei>:<zeile>` | verletztes `NL_ASSERT` |
| `NL_CRASH id=<n> pc=… lr=…` | CPU-Ausnahme, Absturzbildschirm aktiv |
| `NL_CONFIG ok\|default` | `config.ini` gelesen bzw. mit Standardwerten angelegt |
| `NL_MOUNT <lib>` | Bibliotheksgerät (`sd:` oder `usb:`) |
| `NL_LIBRARY games=<n> source=<index\|scan>` | Bibliothek bereit |
| `NL_SCAN_DONE games=<n> added=<a> removed=<r>` | Scan fertig (erster oder Hintergrund-Scan) |
| `NL_INDEX_SAVED` | `index.bin` geschrieben |
| `NL_FAV_SAVED <schlüssel>` | Favoriten gespeichert |
| `NL_SEARCH mario=<n>` | Selbsttest: Treffer der Suche „MARIO“ |
| `NL_EMULATORS ok\|default` | `emulators.ini` gelesen bzw. Standardwerte genutzt |
| `NL_PREFLIGHT_FAIL <code> <schlüssel>` | Start abgelehnt (z. B. `Unsupported`, `NotFound`), Dialog offen |
| `NL_HISTORY_SAVED` | Verlauf vor dem Start gespeichert |
| `NL_LAUNCH <schlüssel>` | Chainload beginnt |
| `NL_AUTOSTART <schlüssel>` | Testmodus: `[debug] autostart` in `config.ini` startet das Spiel nach `NL_READY` |
| `NL_ARGV argc=<n>`, `NL_ARGV <i> <wert>` | Test-DOL `argv_echo`: empfangene Argumente |
| `NL_WARN <text>` | nicht kritischer Fehler (z. B. kaputter Cache verworfen) |

## Schriften

M PLUS Rounded 1c (SIL Open Font License 1.1, siehe `assets/fonts/OFL.txt`), auf Latin-1 plus „…" reduziert.
