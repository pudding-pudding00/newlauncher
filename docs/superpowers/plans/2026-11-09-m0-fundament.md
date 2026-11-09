# M0 – Fundament: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein lauffähiges Projektgerüst: Host-Tests und Wii-DOL bauen per CMake, eine „Hallo GX“-DOL rendert in Dolphin, ein automatischer Smoke-Test meldet `SMOKE OK`, und CI baut jeden Push.

**Architecture:** Ein CMake-Projekt mit zwei Presets: `host` (WinLibs-GCC + Ninja → doctest-Tests für `src/core/`) und `wii` (devkitPPC über `Wii.cmake` → `newlauncher.dol`). Python-Skripte (nur Standardbibliothek) kapseln den Wii-Build unter Windows, die Dolphin-Dev-Konfiguration, das Deployment auf die virtuelle SD und den Smoke-Test.

**Tech Stack:** C++20, devkitPPC/libogc (wiiuse, bte), CMake ≥ 3.20 mit Presets v6, Ninja, doctest 2.4.11, Python 3 (unittest), Dolphin 2609b, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-09-wii-launcher-design.md`

## Global Constraints

- C++20, `-fno-exceptions -fno-rtti -Wall -Wextra` für Wii- **und** Host-Code.
- `src/core/` enthält kein `#include <ogc/...>`, `<wiiuse/...>` oder `<gccore.h>`.
- Logisches Koordinatensystem 640×480 (16:9-Korrektur folgt in M1).
- Theme-Farben für „Hallo GX“: Hintergrund `#EEF1F4`, Akzent `#34BEED`.
- Dolphin wird **immer** mit `-u <dev-user-dir>` gestartet; Dev-User-Ordner: `../wii_tools/dolphin-user/`, SD-Sync-Ordner darin: `SDSync/`.
- Smoke-Marker: Zeile `NL_READY` über `SYS_Report`; Smoke-Timeout 60 s.
- Python-Werkzeuge nutzen nur die Standardbibliothek.
- Keine ROMs, ISOs, WADs im Repo. Zeilenenden LF.
- Lizenz GPLv3.
- Arbeitsbranch: `feat/m0-fundament`, abgezweigt von `docs/launcher-spec`.
- Jede Commit-Nachricht endet mit `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Gepusht wird nur nach ausdrücklicher Freigabe durch den Nutzer.

## Review Focus

1. **Altes Log mit `NL_READY` von einem früheren Lauf** → der Smoke-Test darf nicht fälschlich „OK“ melden; das Log wird vor jedem Start gelöscht (Task 3, `test_reset_log_removes_stale_marker`).
2. **Dolphin hängt oder stürzt ab, ohne `NL_READY`** → Smoke endet nach Timeout bzw. sofort bei Prozessende mit Exit-Code 1, ohne zurückbleibenden Dolphin-Prozess (Task 3, `test_wait_for_marker_times_out`, `test_wait_for_marker_stops_when_process_dies`).
3. **Dolphin oder DOL fehlt** → klare Meldung und Exit-Code 2 statt Traceback (Task 3, `test_smoke_reports_missing_dolphin`).
4. **Die private Dolphin-Konfiguration des Nutzers (AppData) wird verändert** → jeder Dolphin-Aufruf enthält `-u` (Task 3, `test_dolphin_command_always_isolates_user_dir`).
5. **Repo-Pfad mit Leerzeichen** → die Umwandlung in einen MSYS-Pfad für den Wii-Build bleibt korrekt (Task 2, `test_to_msys_path_keeps_spaces`).

---

## Dateistruktur nach M0

```
.gitignore  .gitattributes  LICENSE  README.md
CMakeLists.txt  CMakePresets.json
cmake/NlFlags.cmake                    gemeinsame Compiler-Flags
third_party/doctest/doctest.h          doctest 2.4.11 (vendored)
src/core/result.hpp                    Result<T, E>, Error, ErrorCode
src/core/color.hpp                     Color, Color::fromHex
src/platform/log.hpp|.cpp              Log über SYS_Report (Dolphin-UART)
src/platform/video.hpp|.cpp            VI: Videomodus, Framebuffer, Präsentation
src/gfx/gx_context.hpp|.cpp            GX: Init, Frame, fillRect (Test-Quad)
src/app/main.cpp                       „Hallo GX“-Hauptschleife
dist/apps/newlauncher/meta.xml         HBC-Eintrag mit <ahb_access/>
tests/host/main.cpp                    doctest-Main
tests/host/core/result_test.cpp
tests/host/core/color_test.cpp
tools/devkitpro.py                     MSYS-Pfad + Wii-Build-Aufruf
tools/build_wii.py                     CLI: Wii-Build
tools/dolphin.py                       Dolphin-Dev-Konfiguration, Log, Marker
tools/deploy_dolphin.py                DOL + dist/ → SD-Sync-Ordner
tools/dolphin_smoke.py                 Smoke-Test
tools/tests/test_devkitpro.py
tools/tests/test_dolphin.py
.github/workflows/build.yml
```

---

### Task 1: Host-Build mit `Result` und `Color`

**Files:**
- Create: `.gitignore`, `.gitattributes`, `LICENSE`, `CMakeLists.txt`, `CMakePresets.json`, `cmake/NlFlags.cmake`, `third_party/doctest/doctest.h`, `src/core/result.hpp`, `src/core/color.hpp`
- Test: `tests/host/main.cpp`, `tests/host/core/result_test.cpp`, `tests/host/core/color_test.cpp`

**Interfaces:**
- Produces:
  - `namespace nl { enum class ErrorCode : std::uint16_t { NotFound, IoError, InvalidData, VersionMismatch, NotMounted, Timeout, Unsupported }; }`
  - `struct nl::Error { ErrorCode code; std::string context; };`
  - `template <typename T, typename E = Error> class nl::Result` mit `static Result ok(T)`, `static Result err(E)`, `bool isOk() const`, `T& value() &`, `const T& value() const&`, `T&& value() &&`, `const E& error() const`, `T valueOr(T fallback) const&`
  - Spezialisierung `nl::Result<void, E>` mit `static Result ok()`, `static Result err(E)`, `bool isOk() const`, `const E& error() const`
  - `struct nl::Color { std::uint8_t r, g, b, a; static constexpr Color fromHex(std::uint32_t rgb, std::uint8_t alpha = 0xFF); }` sowie `operator==`
  - CMake-Funktion `nl_apply_flags(<target>)`, Interface-Bibliothek `nl_core` (Include-Pfad `src/`)
  - Presets `host` (configure/build/test)

- [ ] **Step 1: Branch anlegen und Host-Toolchain installieren**

```bash
git switch -c feat/m0-fundament docs/launcher-spec
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e --accept-package-agreements
winget install --id Ninja-build.Ninja -e --accept-package-agreements
```
In einer **neuen** Shell prüfen: `g++ --version` → `g++ (MinGW-W64 x86_64-ucrt-posix-seh …) 16.x`, `ninja --version` → `1.13.x`.

- [ ] **Step 2: Repo-Hygiene anlegen**

- `.gitattributes`: `* text=auto eol=lf`; `*.png`, `*.dol`, `*.elf`, `*.tex`, `*.bin` als `binary`.
- `.gitignore`: `build/`, `__pycache__/`, `.superpowers/`; Spieldateien `*.wbfs *.wbf[0-9] *.iso *.ciso *.wad *.z64 *.n64 *.v64 *.nes *.sfc *.smc *.gb *.gbc *.gba` mit Ausnahme `!tests/fixtures/**`.
- `LICENSE`: GPLv3-Volltext von `https://www.gnu.org/licenses/gpl-3.0.txt`.
- `third_party/doctest/doctest.h`: von `https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h`.

- [ ] **Step 3: CMake-Gerüst**

- `cmake/NlFlags.cmake`: `function(nl_apply_flags target)` setzt `-fno-exceptions -fno-rtti -Wall -Wextra` (PRIVATE).
- `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.20)`, `project(newlauncher VERSION 0.0.1 LANGUAGES C CXX)`, `CMAKE_CXX_STANDARD 20` (required, keine Extensions), `add_library(nl_core INTERFACE)` mit Include `src/`. Zweig `if(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")` bleibt in Task 1 leer; im `else()`-Zweig: `enable_testing()`, Executable `nl_host_tests` aus `tests/host/*.cpp`, Include `third_party/`, Definition `DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS`, `nl_apply_flags`, `add_test(NAME nl_host_tests COMMAND nl_host_tests)`.
- `CMakePresets.json` (Version 6): configurePreset `host` (Generator `Ninja`, `binaryDir` `${sourceDir}/build/host`, `CMAKE_BUILD_TYPE=Debug`, `CMAKE_CXX_COMPILER=g++`, `CMAKE_C_COMPILER=gcc`); buildPreset `host`; testPreset `host` mit `"output": {"outputOnFailure": true}`.
- `tests/host/main.cpp`: `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include <doctest/doctest.h>`.

- [ ] **Step 4: Fehlschlagende Tests schreiben**

`tests/host/core/result_test.cpp`:
```cpp
#include "core/result.hpp"
#include <doctest/doctest.h>
#include <memory>
using nl::Error; using nl::ErrorCode; using nl::Result;

TEST_CASE("Result hält einen Wert") {
    auto r = Result<int>::ok(42);
    CHECK(r.isOk());
    CHECK(r.value() == 42);
}
TEST_CASE("Result hält einen Fehler mit Kontext") {
    auto r = Result<int>::err(Error{ErrorCode::NotFound, "sd:/apps/x/boot.dol"});
    CHECK_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::NotFound);
    CHECK(r.error().context == "sd:/apps/x/boot.dol");
}
TEST_CASE("valueOr liefert den Ersatzwert nur im Fehlerfall") {
    CHECK(Result<int>::ok(7).valueOr(0) == 7);
    CHECK(Result<int>::err(Error{ErrorCode::IoError, ""}).valueOr(5) == 5);
}
TEST_CASE("Result<void>") {
    CHECK(Result<void>::ok().isOk());
    auto r = Result<void>::err(Error{ErrorCode::Timeout, "usb"});
    CHECK_FALSE(r.isOk());
    CHECK(r.error().code == ErrorCode::Timeout);
}
TEST_CASE("Result unterstützt move-only Typen") {
    auto r = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(3));
    REQUIRE(r.isOk());
    std::unique_ptr<int> p = std::move(r).value();
    CHECK(*p == 3);
}
```

`tests/host/core/color_test.cpp`:
```cpp
#include "core/color.hpp"
#include <doctest/doctest.h>
using nl::Color;

TEST_CASE("Color::fromHex zerlegt RGB, Alpha standardmäßig deckend") {
    constexpr Color c = Color::fromHex(0x34BEED);
    static_assert(c.r == 0x34 && c.g == 0xBE && c.b == 0xED && c.a == 0xFF);
    CHECK(c == Color{0x34, 0xBE, 0xED, 0xFF});
}
TEST_CASE("Color::fromHex übernimmt Alpha") {
    CHECK(Color::fromHex(0x000000, 0x73) == Color{0, 0, 0, 0x73});
}
```

- [ ] **Step 5: Test ausführen, Fehlschlag prüfen**

Run: `cmake --preset host && cmake --build --preset host`
Expected: Build-Fehler `core/result.hpp: No such file or directory`.

- [ ] **Step 6: `src/core/result.hpp` und `src/core/color.hpp` implementieren**

`Result` speichert Wert oder Fehler ohne Exceptions (z. B. `std::variant` mit `std::get_if`, kein `std::get`). `value()` bzw. `error()` im falschen Zustand ist ein Programmierfehler: Debug-`assert`, keine Exception.

- [ ] **Step 7: Tests ausführen, Erfolg prüfen**

Run: `cmake --build --preset host && ctest --preset host`
Expected: `100% tests passed, 0 tests failed out of 1`; doctest meldet `7 test cases … 0 failed`.

- [ ] **Step 8: Commit**

```bash
git add .gitignore .gitattributes LICENSE CMakeLists.txt CMakePresets.json cmake third_party src/core tests/host
git commit -m "build: Host-Build mit doctest, Result und Color"
```

---

### Task 2: Wii-Build und „Hallo GX“-DOL

**Files:**
- Create: `tools/devkitpro.py`, `tools/build_wii.py`, `tools/tests/test_devkitpro.py`, `src/platform/log.hpp|.cpp`, `src/platform/video.hpp|.cpp`, `src/gfx/gx_context.hpp|.cpp`, `src/app/main.cpp`, `dist/apps/newlauncher/meta.xml`
- Modify: `CMakeLists.txt` (Wii-Zweig), `CMakePresets.json` (Preset `wii`)

**Interfaces:**
- Consumes: `nl::Color` (Task 1), `nl_apply_flags`, `nl_core`
- Produces:
  - `tools/devkitpro.py`: `to_msys_path(path: str) -> str`; `wii_build_invocation(repo_root: Path, platform: str) -> tuple[list[str], dict[str, str]]` (Befehl + zusätzliche Umgebungsvariablen); Konstante `DEVKITPRO_BASH = r"C:\devkitPro\msys2\usr\bin\bash.exe"`
  - `tools/build_wii.py`: `main() -> int`
  - `namespace nl::log { void init(); void line(const char* fmt, ...); }`
  - `class nl::platform::Video { void init(); GXRModeObj* mode() const; void* framebuffer(int index) const; bool widescreen() const; void present(int index); }`
  - `class nl::gfx::GxContext { void init(platform::Video& video); void beginFrame(Color clear); void fillRect(float x, float y, float w, float h, Color color); void endFrame(); }`
  - Build-Ergebnis `build/wii/newlauncher.dol`

- [ ] **Step 1: devkitPro installieren (Nutzer)**

1. `devkitProUpdater-3.0.3.exe` von `https://github.com/devkitPro/installer/releases/tag/v3.0.3` herunterladen und ausführen; Zielordner `C:\devkitPro`, Komponente **„Wii Development“** auswählen.
2. Danach in PowerShell:
```powershell
& C:\devkitPro\msys2\usr\bin\bash.exe -lc "pacman -Syu --noconfirm && pacman -S --needed --noconfirm wii-dev cmake"
```
3. Prüfen:
```powershell
& C:\devkitPro\msys2\usr\bin\bash.exe -lc 'echo $DEVKITPRO; $DEVKITPPC/bin/powerpc-eabi-g++ --version | head -1; cmake --version | head -1; ls $DEVKITPRO/cmake/Wii.cmake'
```
Expected: `/opt/devkitpro`, eine `powerpc-eabi-g++`-Versionszeile, `cmake version` ≥ 3.25, Pfad zu `Wii.cmake`.

- [ ] **Step 2: Fehlschlagende Tests für `tools/devkitpro.py` schreiben**

`tools/tests/test_devkitpro.py` (unittest; der Test fügt `tools/` per `sys.path` hinzu):
```python
def test_to_msys_path_converts_drive(self):
    self.assertEqual(to_msys_path(r"<repo-root>"),
                     "/path/to/newlauncher")
def test_to_msys_path_keeps_spaces(self):
    self.assertEqual(to_msys_path(r"D:\Mein Ordner\repo"), "/d/Mein Ordner/repo")
def test_windows_invocation_uses_devkitpro_bash_in_repo(self):
    cmd, env = wii_build_invocation(Path(r"C:\x\repo"), "win32")
    self.assertEqual(cmd[0], DEVKITPRO_BASH)
    self.assertEqual(cmd[1], "-lc")
    self.assertIn("cmake --preset wii && cmake --build --preset wii", cmd[2])
    self.assertEqual(env, {"CHERE_INVOKING": "1"})
def test_linux_invocation_runs_cmake_directly(self):
    cmd, env = wii_build_invocation(Path("/src/repo"), "linux")
    self.assertEqual(cmd, ["sh", "-c", "cmake --preset wii && cmake --build --preset wii"])
    self.assertEqual(env, {})
```

- [ ] **Step 3: Tests ausführen, Fehlschlag prüfen**

Run: `python -m unittest discover -s tools/tests -v`
Expected: `ModuleNotFoundError: No module named 'devkitpro'`.

- [ ] **Step 4: `tools/devkitpro.py` und `tools/build_wii.py` implementieren**

`build_wii.py` ruft `wii_build_invocation(repo_root, sys.platform)` auf, startet den Befehl mit `cwd=repo_root` und um `env` ergänzter Umgebung und gibt dessen Exit-Code zurück.

- [ ] **Step 5: Tests ausführen, Erfolg prüfen**

Run: `python -m unittest discover -s tools/tests -v`
Expected: `Ran 4 tests … OK`.

- [ ] **Step 6: Wii-Preset und Wii-Zweig in CMake**

- `CMakePresets.json`: configurePreset `wii` (Generator `Unix Makefiles`, `binaryDir` `${sourceDir}/build/wii`, `toolchainFile` `$env{DEVKITPRO}/cmake/Wii.cmake`, `CMAKE_BUILD_TYPE=Release`); buildPreset `wii`.
- `CMakeLists.txt`, Zweig `NintendoWii`: `add_executable(newlauncher src/app/main.cpp src/platform/log.cpp src/platform/video.cpp src/gfx/gx_context.cpp)`, `target_link_libraries(newlauncher PRIVATE nl_core wiiuse bte)`, `target_compile_definitions(newlauncher PRIVATE NL_VERSION="${PROJECT_VERSION}")`, `nl_apply_flags(newlauncher)`, `ogc_create_dol(newlauncher)`.

- [ ] **Step 7: Plattform- und Grafikcode implementieren**

- `log.cpp`: `init()` ruft `SYS_STDIO_Report(true)`; `line()` formatiert mit `vsnprintf` in einen 256-Byte-Puffer und ruft `SYS_Report("%s\n", buf)` (libogc übersetzt `\n` in das `\r`, bei dem Dolphin eine Logzeile abschließt).
- `video.cpp`: `VIDEO_Init`, `VIDEO_GetPreferredMode(nullptr)`, zwei Framebuffer über `MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode))`, `VIDEO_Configure`, `VIDEO_SetBlack(false)`, `VIDEO_Flush`, `VIDEO_WaitVSync` (bei interlaced zweimal); `widescreen()` = `CONF_GetAspectRatio() == CONF_ASPECT_16_9`; `present(i)` = `VIDEO_SetNextFramebuffer(xfb[i])`, `VIDEO_Flush`, `VIDEO_WaitVSync`.
- `gx_context.cpp`: 256-KB-FIFO (32-Byte-ausgerichtet), `GX_Init`, Viewport/Scissor/Copy-Einstellungen aus `mode()`, Vertex-Format Position (F32, XY) + Farbe (RGBA8), TEV `GX_PASSCLR`, Orthoprojektion `guOrtho(proj, 0, 479, 0, 639, 0, 300)` (Ursprung oben links). `beginFrame` setzt `GX_SetCopyClear(clear, GX_MAX_Z24)`; `endFrame` = `GX_DrawDone`, `GX_CopyDisp` in den nächsten Framebuffer, `video.present(next)`, Puffer wechseln. `fillRect` zeichnet ein `GX_QUADS` mit 4 Vertices.
- `main.cpp`: `log::init()`, `log::line("newlauncher %s", NL_VERSION)`, `Video::init`, `GxContext::init`, `WPAD_Init`, `PAD_Init`. Schleife: Pads scannen; HOME (Wiimote 0) oder START (GC-Port 0) beendet mit `return 0`. Jeder Frame: `beginFrame(Color::fromHex(0xEEF1F4))`, `fillRect(220, 180, 200, 120, Color::fromHex(0x34BEED))`, `endFrame()`. Nach dem **ersten** `endFrame()` genau einmal `log::line("NL_READY")`.
- `dist/apps/newlauncher/meta.xml`: `<app version="1">` mit `name` „newlauncher“, `coder` „pudding-pudding00“, `version` „0.0.1“, `short_description` „Wii-Launcher“, und `<ahb_access/>`.

- [ ] **Step 8: Wii-Build ausführen**

Run: `python tools/build_wii.py`
Expected: Exit-Code 0, Ausgabe enthält `Converting newlauncher to .dol format`, Datei `build/wii/newlauncher.dol` existiert. Host-Build bleibt grün: `cmake --build --preset host && ctest --preset host`.

- [ ] **Step 9: Sichtprüfung in Dolphin**

Run: `..\wii_tools\Dolphin-x64\Dolphin.exe -u ..\wii_tools\dolphin-user -e build\wii\newlauncher.dol`
Expected: hellgrauer Bildschirm (`#EEF1F4`) mit blauem Rechteck (`#34BEED`) mittig; Fenster schließen beendet.

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt CMakePresets.json src tools/devkitpro.py tools/build_wii.py tools/tests/test_devkitpro.py dist
git commit -m "feat: Wii-Build und Hallo-GX-DOL"
```

---

### Task 3: Dolphin-Dev-Werkzeuge und Smoke-Test

**Files:**
- Create: `tools/dolphin.py`, `tools/deploy_dolphin.py`, `tools/dolphin_smoke.py`
- Test: `tools/tests/test_dolphin.py`

**Interfaces:**
- Consumes: `build/wii/newlauncher.dol`, `dist/` (Task 2)
- Produces:
  - `tools/dolphin.py`: `REPO_ROOT: Path`, `DEFAULT_DOLPHIN = REPO_ROOT.parent / "wii_tools/Dolphin-x64/Dolphin.exe"`, `DEFAULT_USER_DIR = REPO_ROOT.parent / "wii_tools/dolphin-user"`, `sd_sync_dir(user_dir: Path) -> Path` (= `user_dir / "SDSync"`), `log_path(user_dir: Path) -> Path` (= `user_dir / "Logs" / "dolphin.log"`), `write_dev_config(user_dir: Path) -> None`, `dolphin_command(exe: Path, user_dir: Path, dol: Path) -> list[str]`, `reset_log(user_dir: Path) -> None`, `wait_for_marker(log: Path, marker: str, timeout_s: float, is_alive: Callable[[], bool], poll_s: float = 0.25) -> bool`
  - `tools/deploy_dolphin.py`: `deploy(dol: Path, sd_dir: Path, dist_dir: Path) -> None`, `main() -> int`
  - `tools/dolphin_smoke.py`: `main(argv: list[str] | None = None) -> int` (0 = OK, 1 = Marker fehlt, 2 = Voraussetzung fehlt)

`write_dev_config` schreibt (überschreibt) genau diese zwei Dateien:

`<user>/Config/Dolphin.ini`
```ini
[Core]
WiiSDCard = True
WiiSDCardEnableFolderSync = True
WiiSDCardAllowWrites = True
[General]
WiiSDCardSyncFolder = <sd_sync_dir(user_dir), absolut>
[Interface]
ConfirmStop = False
UsePanicHandlers = False
[Analytics]
Enabled = False
PermissionAsked = True
[AutoUpdate]
UpdateTrack =
```
`<user>/Config/Logger.ini`
```ini
[Options]
WriteToFile = True
WriteToConsole = False
WriteToWindow = False
Verbosity = 1
[Logs]
OSREPORT = True
OSREPORT_HLE = True
```

- [ ] **Step 1: Fehlschlagende Tests schreiben**

`tools/tests/test_dolphin.py` (unittest, jeweils mit `tempfile.TemporaryDirectory`; INI-Prüfung mit `configparser` und `optionxform = str`):
```python
def test_write_dev_config_sets_sd_sync_and_logger(self):
    write_dev_config(user)
    d = read_ini(user / "Config/Dolphin.ini"); l = read_ini(user / "Config/Logger.ini")
    self.assertEqual(d["Core"]["WiiSDCardEnableFolderSync"], "True")
    self.assertEqual(d["General"]["WiiSDCardSyncFolder"], str(user / "SDSync"))
    self.assertEqual(d["Analytics"]["PermissionAsked"], "True")
    self.assertEqual(l["Options"]["WriteToFile"], "True")
    self.assertEqual(l["Logs"]["OSREPORT"], "True")
def test_dolphin_command_always_isolates_user_dir(self):
    cmd = dolphin_command(Path("D.exe"), user, Path("a.dol"))
    self.assertEqual(cmd, ["D.exe", "-u", str(user), "-b", "-e", "a.dol"])
def test_reset_log_removes_stale_marker(self):
    log_path(user).parent.mkdir(parents=True); log_path(user).write_text("NL_READY\n")
    reset_log(user)
    self.assertFalse(log_path(user).exists())
def test_wait_for_marker_finds_marker(self):
    log.write_text("OSREPORT: newlauncher 0.0.1\nOSREPORT: NL_READY\n")
    self.assertTrue(wait_for_marker(log, "NL_READY", 2.0, lambda: True))
def test_wait_for_marker_times_out(self):
    log.write_text("OSREPORT: newlauncher 0.0.1\n"); t = time.monotonic()
    self.assertFalse(wait_for_marker(log, "NL_READY", 0.5, lambda: True))
    self.assertLess(time.monotonic() - t, 2.0)
def test_wait_for_marker_stops_when_process_dies(self):
    t = time.monotonic()
    self.assertFalse(wait_for_marker(log, "NL_READY", 30.0, lambda: False))
    self.assertLess(time.monotonic() - t, 1.0)
def test_deploy_places_boot_dol_and_meta(self):
    deploy(dol, sd, dist)   # dist enthält apps/newlauncher/meta.xml
    self.assertTrue((sd / "apps/newlauncher/boot.dol").is_file())
    self.assertTrue((sd / "apps/newlauncher/meta.xml").is_file())
def test_smoke_reports_missing_dolphin(self):
    self.assertEqual(smoke_main(["--dolphin", str(tmp / "fehlt.exe"), "--dol", str(dol),
                                 "--user-dir", str(user)]), 2)
```

- [ ] **Step 2: Tests ausführen, Fehlschlag prüfen**

Run: `python -m unittest discover -s tools/tests -v`
Expected: `ModuleNotFoundError: No module named 'dolphin'` (die 4 Tests aus Task 2 laufen weiter).

- [ ] **Step 3: `dolphin.py`, `deploy_dolphin.py`, `dolphin_smoke.py` implementieren**

`dolphin_smoke.main`: Argumente `--dolphin` (Standard `DEFAULT_DOLPHIN`), `--user-dir` (`DEFAULT_USER_DIR`), `--dol` (`build/wii/newlauncher.dol`), `--timeout` (60). Fehlt Dolphin oder DOL: `SMOKE FEHLER: <Dolphin|DOL> nicht gefunden: <pfad>`, Rückgabe 2. Sonst: `write_dev_config` → `deploy` → `reset_log` → `subprocess.Popen(dolphin_command(...))` → `wait_for_marker(..., is_alive=lambda: proc.poll() is None)` → im `finally` `terminate()`, bis 5 s warten, dann `kill()`. Ausgabe `SMOKE OK` (0) oder `SMOKE FEHLGESCHLAGEN: NL_READY nicht innerhalb von <n> s` (1).

- [ ] **Step 4: Tests ausführen, Erfolg prüfen**

Run: `python -m unittest discover -s tools/tests -v`
Expected: `Ran 12 tests … OK`.

- [ ] **Step 5: Smoke-Test gegen echte DOL**

Run: `python tools/build_wii.py && python tools/dolphin_smoke.py`
Expected: `SMOKE OK` innerhalb von 60 s; danach läuft kein `Dolphin.exe`-Prozess mehr (`Get-Process Dolphin -ErrorAction SilentlyContinue` liefert nichts). In `../wii_tools/dolphin-user/SDSync/apps/newlauncher/` liegen `boot.dol` und `meta.xml`.

- [ ] **Step 6: Commit**

```bash
git add tools/dolphin.py tools/deploy_dolphin.py tools/dolphin_smoke.py tools/tests/test_dolphin.py
git commit -m "test: Dolphin-Dev-Setup und Smoke-Test"
```

---

### Task 4: CI und Build-Anleitung

**Files:**
- Create: `.github/workflows/build.yml`
- Modify: `README.md`

**Interfaces:**
- Consumes: Presets `host`, `wii` (Tasks 1–2), `tools/tests` (Tasks 2–3)
- Produces: Workflow `build` mit Jobs `wii` und `host-tests`; Artefakt `newlauncher-dol`

- [ ] **Step 1: Workflow schreiben**

`.github/workflows/build.yml`, Trigger `push` und `pull_request`:
- Job `wii`: `runs-on: ubuntu-latest`, `container: devkitpro/devkitppc:20261102`; Schritte: `actions/checkout@v4`; `apt-get update && apt-get install -y --no-install-recommends cmake`; `cmake --preset wii && cmake --build --preset wii`; `actions/upload-artifact@v4` mit Name `newlauncher-dol`, Pfad `build/wii/newlauncher.dol`.
- Job `host-tests`: `runs-on: ubuntu-latest`; Schritte: `actions/checkout@v4`; `sudo apt-get install -y ninja-build`; `cmake --preset host && cmake --build --preset host && ctest --preset host`; `python3 -m unittest discover -s tools/tests -v`.

- [ ] **Step 2: README ergänzen**

Abschnitte: Voraussetzungen (devkitPro mit „Wii Development“, WinLibs-GCC, Ninja, CMake, Python 3, Dolphin unter `../wii_tools/`), Befehle für Host-Tests, Wii-Build, Dolphin-Start und Smoke-Test (wie in Tasks 1–3), Verweis auf Spec und Lizenz.

- [ ] **Step 3: Lokal gegenprüfen**

Run: `cmake --build --preset host && ctest --preset host && python -m unittest discover -s tools/tests -v && python tools/build_wii.py && python tools/dolphin_smoke.py`
Expected: alle Tests grün, `SMOKE OK`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build.yml README.md
git commit -m "ci: Wii-Build und Host-Tests in GitHub Actions"
```

- [ ] **Step 5: CI prüfen (nur nach Freigabe durch den Nutzer)**

Run: `git push -u origin feat/m0-fundament` und anschließend `gh run watch`
Expected: Workflow `build` mit `wii` und `host-tests` grün; Artefakt `newlauncher-dol` vorhanden.
