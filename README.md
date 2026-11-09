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

Wii-Build (`build/wii/newlauncher.dol`):
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
