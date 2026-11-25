# M21: Rückkehr aus Wii-Spielen, Cover automatisch – Umsetzungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** „Wii-Menü“ in einem von newlauncher gestarteten Wii-Spiel/Kanal führt zurück in newlauncher; fehlende Wii/GC-Cover laden nach dem Start automatisch.

**Architecture:** Der M20-Rückkehr-Stub (`0x80001800`) wird auch vor Wii-Spielen installiert. Der Booter erkennt ihn am gültigen Parameterblock und biegt in `__OSLaunchMenu` den `ES_LaunchTitle`-Aufruf auf den Stub. newlauncher mit `--from-game` hebt die d2x-Reload-Sperre auf und lädt IOS 58. Der Cover-Download bekommt einen stillen Autostart hinter einer neuen Einstellung.

**Tech Stack:** C++20/libogc (Launcher), C/libogc (WiiFlow-Booter), freistehendes C (Stub), doctest (Host), Python (Smokes).

**Spec:** `docs/superpowers/specs/2026-11-25-m21-rueckkehr-cover-design.md`

## Global Constraints

- `src/core/` bindet kein `<ogc/...>` ein; kein `throw`, kein RTTI.
- Wii-Build mit 0 Warnungen, mit TLS (`python tools/build_wii.py`) und ohne (`--no-tls`).
- UI-Texte DE und EN.
- Kein Schreiben auf den NAND. Kein Common Key.
- Stub-Argument heißt exakt `--from-game`; Log-Marken `NL_FROM_GAME`, `NL_COVER_AUTO jobs=<n>`.
- Config-Schlüssel `[covers] auto`, Standard `1`.
- Spiel-Patch an `ad[2]`: `lis r3,0x8000` / `ori r3,r3,0x1800` / `mtctr r3` / `bctr` = `0x3C608000 0x60631800 0x7C6903A6 0x4E800420`.
- d2x-Sperre aufheben: `/dev/es` ioctlv `0xA0`, zwei Eingaben (`mode = 0`, `ios = IOS_GetVersion()`), danach `IOS_ReloadIOS(58)`.

## Review Focus

- Cheats aktiv → kein Stub, Booter-Codehandler leert `0x80001800…0x80003000`; Rückkehr fällt auf `returnTo` zurück (Test in Task 1).
- newlauncher per wiiload (`argv[0]` nicht `sd:/…`) → kein Stub (Test in Task 1).
- `--from-game` steht nicht an Index 1 (z. B. nach `--remote`) → trotzdem erkannt (Test in Task 1).
- Smokes ohne Cover-Server dürfen nicht ins Internet laden → `auto = 0` in jeder Smoke-Config außer `--cover-auto` (Task 5).
- Automatischer Lauf ohne fehlende Cover → kein Dialog „Keine fehlenden Cover“ (Smoke in Task 5, Log `NL_COVER_AUTO jobs=0`).

---

### Task 1: Core – Argument-Flag und Stub-Entscheidung

**Files:**
- Modify: `src/core/launch_args.hpp`, `src/core/launch_args.cpp`
- Modify: `src/core/return_stub.hpp`, `src/core/return_stub.cpp`
- Test: `tests/host/core/launch_args_test.cpp`, `tests/host/core/return_stub_test.cpp`

**Interfaces:**
- Produces: `bool nl::hasArgFlag(int argc, const char* const* argv, std::string_view flag);` (alle Indizes inkl. 0, `argv == nullptr` → false)
- Produces: `inline constexpr std::string_view nl::kFromGameFlag = "--from-game";` (in `return_stub.hpp`)
- Produces: `bool nl::gameReturnStubWanted(std::string_view selfDol, std::size_t gctBytes);` → `selfDol` beginnt mit `sd:/` und `gctBytes == 0`

- [ ] **Step 1: Tests schreiben**

```cpp
TEST_CASE("hasArgFlag: an jeder Stelle, nullptr sicher") {
    const char* a[] = {"sd:/apps/newlauncher/boot.dol", "--remote", "--from-game"};
    CHECK(nl::hasArgFlag(3, a, "--from-game"));
    CHECK(nl::hasArgFlag(3, a, "--remote"));
    CHECK_FALSE(nl::hasArgFlag(2, a, "--from-game"));
    CHECK_FALSE(nl::hasArgFlag(0, nullptr, "--from-game"));
}
TEST_CASE("gameReturnStubWanted: nur von SD und ohne Cheats") {
    CHECK(nl::gameReturnStubWanted("sd:/apps/newlauncher/boot.dol", 0));
    CHECK_FALSE(nl::gameReturnStubWanted("sd:/apps/newlauncher/boot.dol", 16));
    CHECK_FALSE(nl::gameReturnStubWanted("usb:/apps/newlauncher/boot.dol", 0));
    CHECK_FALSE(nl::gameReturnStubWanted("", 0));
}
```

- [ ] **Step 2:** `ctest --preset host` → beide Fälle schlagen fehl (nicht definiert).
- [ ] **Step 3:** Funktionen implementieren; `hasRemoteFlag` (remote_proto.cpp) auf `hasArgFlag(argc, argv, "--remote")` umstellen.
- [ ] **Step 4:** `cmake --build --preset host && ctest --preset host` → alles grün.
- [ ] **Step 5:** Commit `feat: hasArgFlag und gameReturnStubWanted (M21)`.

### Task 2: Booter – Sprung zum Stub statt Systemmenü

**Files:**
- Modify: `boot/source/patchcode.c` (`PatchReturnTo`), `boot/source/patchcode.h`
- Modify: `boot/source/main.cpp` (Erkennung, Disc-ID bei `0x80001800`)
- Modify: `CMakeLists.txt:278-280` (Include `src/launch/return_stub` für `nl_game_booter`, falls `src` nicht reicht)
- Modify: `boot/UPSTREAM.md` (Änderungstabelle)

**Interfaces:**
- Produces: `extern u8 returnToStub;` in `patchcode.h`; `PatchReturnTo` patcht, wenn `returnToStub != 0`, auch bei `id == 0`.

- [ ] **Step 1:** In `main.cpp` nach dem Lesen von `normalCFG`: `returnToStub = hooktype == 0 && nlrs_params_ok((const unsigned char*)NLRS_PARAMS_ADDR);` (`#include "launch/return_stub/stub_check.h"`), `gprintf` mit dem Ergebnis.
- [ ] **Step 2:** `PatchReturnTo`: Abbruch nur noch bei `(!id && !returnToStub) || returnToPatched`. Im Treffer-Zweig bei `returnToStub` nur `ad[2]` mit den vier Wörtern aus den Global Constraints überschreiben (kein Schreiben nach `ad[3]`, `ad[0]`/`ad[1]` unverändert), sonst der alte Weg. `gprintf("Return to stub patched.\n")`.
- [ ] **Step 3:** `memcpy((void*)0x80001800, Disc_ID, 8)` nur ausführen, wenn `!returnToStub`.
- [ ] **Step 4:** `python tools/build_wii.py` und `--no-tls` → exit 0, 0 Warnungen. (Spec Punkt 3, Interrupts im Stub aus: erledigt `crt0.S` schon vor `stub_main`, keine Änderung am Stub.)
- [ ] **Step 5:** UPSTREAM.md-Zeile für `patchcode.c`/`main.cpp` ergänzen; Commit `feat: Booter springt bei „Wii-Menü“ in den Rückkehr-Stub (M21)`.

### Task 3: Launcher – Stub vor Wii-Spielen, Erholung mit `--from-game`

**Files:**
- Modify: `src/app/main.cpp` (Wii-Zweig ab Zeile 1229; Anfang von `main`)
- Modify: `src/launch/return_stub.hpp`, `src/launch/return_stub.cpp`

**Interfaces:**
- Consumes: `nl::gameReturnStubWanted`, `nl::kFromGameFlag`, `nl::hasArgFlag` (Task 1); `installReturnStub(selfDol, extraArgs)` (vorhanden).
- Produces: `void nl::launch::recoverIosAfterGame();` – d2x-Sperre aufheben (siehe Global Constraints), `IOS_ReloadIOS(58)` falls `IOS_GetVersion() != 58`, Log `NL_FROM_GAME ios=<vorher>-><nachher> unblock=<ret>`.

- [ ] **Step 1:** Wii-Zweig: vor `unmountAll()` – wenn `gameReturnStubWanted(returnStubDol, target.value().wii->gct.size())`, `installReturnStub(returnStubDol, args)` mit `args = {"--from-game"}` plus `"--remote"` bei `keepRemote`; sonst Log `NL_RETURN_STUB skip cheats` bzw. die vorhandene Skip-Meldung.
- [ ] **Step 2:** `recoverIosAfterGame()` implementieren (`IOS_Open("/dev/es")`, 32-Byte-ausgerichtete statische Puffer wie `BlockIOSReload` im Booter, `IOS_Ioctlv(fd, 0xA0, 2, 0, vec)`). Fehler nur loggen.
- [ ] **Step 3:** Ganz am Anfang von `main` (vor `chooseData`, vor Video): `if (nl::hasArgFlag(argc, argv, nl::kFromGameFlag)) nl::launch::recoverIosAfterGame();` Die Log-Zeile entsteht vor dem Logfile und muss im Puffer landen (wie die übrigen frühen Zeilen).
- [ ] **Step 4:** Wii-Builds (TLS / no-TLS) 0 Warnungen; Smokes `--return-stub` und `--remote-check` grün (Emulatorweg unverändert).
- [ ] **Step 5:** Commit `feat: Rückkehr-Stub vor Wii-Spielen, IOS-Wiederherstellung mit --from-game (M21)`.

### Task 4: Core – Einstellung `[covers] auto` und Autostart-Entscheidung

**Files:**
- Modify: `src/core/config.hpp`, `src/core/config.cpp` (lesen, `configValues`, `defaultConfigText`)
- Modify: `src/core/settings_model.hpp`, `src/core/settings_model.cpp` (Zeile `CoverAuto` hinter `CoverRegion`, Umschalten wie `Banners`)
- Modify: `src/core/cover_download.hpp`, `src/core/cover_download.cpp`
- Test: `tests/host/core/config_test.cpp`, `tests/host/core/settings_model_test.cpp`, `tests/host/core/cover_download_test.cpp`

**Interfaces:**
- Produces: `bool Config::coverAuto = true;`
- Produces: `SettingRow::CoverAuto`
- Produces: `struct CoverAutoInputs { bool enabled; bool ready; bool scanRunning; bool netBusy; bool alreadyRan; };` und `bool shouldAutoFetchCovers(const CoverAutoInputs&);` → `enabled && ready && !scanRunning && !netBusy && !alreadyRan`

- [ ] **Step 1: Tests**

```cpp
TEST_CASE("configFromIni: [covers] auto (M21), Standard 1, Round-Trip") {
    CHECK(configFromIni(IniDoc::parse("")).coverAuto);
    CHECK_FALSE(configFromIni(IniDoc::parse("[covers]\nauto = 0\n")).coverAuto);
    CHECK(configFromIni(IniDoc::parse("[covers]\nauto = 7\n")).coverAuto);
    CHECK(defaultConfigText().find("auto = 1") != std::string::npos);
    Config c; c.coverAuto = false;
    CHECK_FALSE(configFromIni(IniDoc::parse(configToIniText(c, ""))).coverAuto);
}
TEST_CASE("stepSetting: CoverAuto schaltet um, ist keine Aktion") {
    Config c; stepSetting(c, SettingRow::CoverAuto, 1);
    CHECK_FALSE(c.coverAuto);
    CHECK_FALSE(isAction(SettingRow::CoverAuto));
}
TEST_CASE("shouldAutoFetchCovers") {
    CHECK(shouldAutoFetchCovers({true, true, false, false, false}));
    CHECK_FALSE(shouldAutoFetchCovers({false, true, false, false, false}));
    CHECK_FALSE(shouldAutoFetchCovers({true, false, false, false, false}));
    CHECK_FALSE(shouldAutoFetchCovers({true, true, true, false, false}));
    CHECK_FALSE(shouldAutoFetchCovers({true, true, false, true, false}));
    CHECK_FALSE(shouldAutoFetchCovers({true, true, false, false, true}));
}
```

- [ ] **Step 2:** `ctest --preset host` → rot.
- [ ] **Step 3:** Implementieren (Parsing `parseIntIn(…, 0, 1, 1)`).
- [ ] **Step 4:** `ctest --preset host` → grün (auch bestehende Settings-Tests, die die Zeilenzahl prüfen, anpassen).
- [ ] **Step 5:** Commit `feat: Einstellung „Cover automatisch laden“ (M21)`.

### Task 5: Launcher – stiller Cover-Autostart, UI-Zeile, Smoke

**Files:**
- Modify: `src/ui/strings.hpp` (`X(CoverAuto, "Cover automatisch laden", "Download covers automatically")`), `src/ui/settings_scene.cpp` (Zeilenliste, Wertanzeige an/aus wie `Banners`)
- Modify: `src/app/main.cpp` (`fetchCovers(bool silent)`; Hauptschleife)
- Modify: `tools/dolphin_smoke.py` (Smoke-Config `[covers] auto = 0` immer; neue Option `--cover-auto`: Cover-Server ohne `fetch_covers`, mit `auto = 1`)
- Test: `tools/tests/` (vorhandene Smoke-Tool-Tests, falls die Config-Erzeugung getestet ist)

**Interfaces:**
- Consumes: `Config::coverAuto`, `shouldAutoFetchCovers`, `SettingRow::CoverAuto` (Task 4).

- [ ] **Step 1:** `fetchCovers` bekommt `bool silent`; bei `silent` kein Dialog bei 0 Jobs, Log `NL_COVER_AUTO jobs=<n>` vor dem Start. Aktion und `[debug] fetch_covers` rufen `fetchCovers(false)`.
- [ ] **Step 2:** In der Hauptschleife nach der Scan-Übernahme: `if (shouldAutoFetchCovers({config.coverAuto, readyReported, worker.running(), net.running(), coverAutoRan})) { coverAutoRan = true; fetchCovers(true); }`. Netz besetzt → nächster Frame versucht es erneut (Nachholen).
- [ ] **Step 3:** Smoke-Tool anpassen; `python -m unittest discover -s tools/tests` grün.
- [ ] **Step 4:** Smokes: `python tools/dolphin_smoke.py --cover-server` (bisher) und `--cover-server --cover-auto` mit `--marker NL_COVER_AUTO` grün; Standard-Smoke ohne Server grün und ohne `NL_COVER_AUTO jobs=` > 0-Netzversuche.
- [ ] **Step 5:** Commit `feat: Cover laden nach dem Start automatisch (M21)`.

### Task 6: Doku, Abnahmeliste, PR

**Files:**
- Create: `docs/acceptance/m21-wii.md` (Punkte G1 RUUE01 → HOME → Wii-Menü → newlauncher mit `NL_FROM_GAME`, USB-Spiele sichtbar; G2 dreimal; G3 mit Cheats → Systemmenü; G4 Kanal → Wii-Menü → newlauncher; G5 GC-Start nach Rückkehr (AHBPROT); C1 Cover erscheinen nach dem Start ohne Aktion)
- Modify: `docs/STATUS.md` (M21-Zeile, Wunsch des Nutzers erledigt bis auf Hardware), Spec v2 7.2 Hinweis auf M21

- [ ] **Step 1:** Dokumente schreiben.
- [ ] **Step 2:** Vollständige Prüfung: Host-Tests, Tool-Tests, beide Wii-Builds 0 Warnungen, `check_licenses.py`, Smokes `--return-stub`, `--remote-check`, `--cover-server`, `--cover-auto`, `--dol build/wii-notls/newlauncher.dol`.
- [ ] **Step 3:** Commit, Push, PR, CI einmal blockierend abwarten, mergen.
