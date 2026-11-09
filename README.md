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

Auf die virtuelle SD deployen bzw. Smoke-Test (wartet auf `NL_READY` im Dolphin-Log):
```bash
python tools/deploy_dolphin.py
python tools/dolphin_smoke.py
```

Selbsttest in Dolphin (geskriptete Eingaben, keine Allokation im Leerlauf, absichtlicher Absturz am Ende):
```bash
python tools/dolphin_smoke.py --dol build/wii/nl_selftest.dol --marker NL_READY --marker "NL_ALLOC_IDLE 0" --marker NL_SELFTEST_OK --marker "NL_CRASH id=7"
```

Schriften neu reduzieren (nur bei Schriftwechsel, braucht `pip install fonttools`):
```bash
python tools/subset_fonts.py MPLUSRounded1c-Regular.ttf MPLUSRounded1c-Medium.ttf
```

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

In M1 schaltet das Zahnrad unten rechts die Darstellung um (Hell → Dunkel → Automatisch, dunkel von 20 bis 7 Uhr).

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

## Schriften

M PLUS Rounded 1c (SIL Open Font License 1.1, siehe `assets/fonts/OFL.txt`), auf Latin-1 plus „…" reduziert.
