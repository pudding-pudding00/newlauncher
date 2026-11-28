# M30: System Menu Channel (`UNLA` Forwarder) & Autostart — Implementation Plan

> **For agentic workers:** Follow TDD (failing test first, then implementation). Complete **all `[HOST]`, `[BUILD]`, and `[DOLPHIN]` tasks first**; defer `[WII-HW]` (NAND/Priiloader/WAD hardware testing) to the consolidated Stage 6 hardware pass in [`2026-11-28-m30-m34-master-roadmap.md`](2026-11-28-m30-m34-master-roadmap.md).

**Goal:** Deliver the standalone **`forwarder.dol`**, launcher **`UNLA`** (`00010001-554E4C41`) channel awareness, Priiloader autostart export & loop-free `"Pune"` System Menu exit, and 100% original channel banner assets + local Wii/vWii WAD build tooling—without ever committing WADs, third-party banners, or keys.

**Spec:** [`docs/superpowers/specs/2026-11-28-m30-channel-autostart-design.md`](../specs/2026-11-28-m30-channel-autostart-design.md)

**Branch:** `feat/m30-channel-autostart`

## Global Constraints
- Never commit `.wad` files, decrypted Nintendo banners, or the Wii Common Key (`AGENTS.md`).
- Never write silently to real Wii NAND; always surface BootMii NAND backup warnings in UI & docs.
- Keep `src/core/` free of `<ogc/...>`; 0 warnings on Wii builds with and without TLS; DE + EN UI strings.

---

### Task 1 `[HOST]`: `UNLA` Title ID Awareness, Library Exclusion & Forwarder Core Helpers
**Files:** Modify `src/core/wii_title.{hpp,cpp}`, `src/core/emunand.cpp`, `src/core/game_options.{hpp,cpp}`, `src/core/options_model.cpp`; Create `src/core/forwarder_plan.{hpp,cpp}`; Update `CMakeLists.txt`; Tests `tests/host/core/wii_title_test.cpp`, `tests/host/core/emunand_test.cpp`, `tests/host/core/game_options_test.cpp`, `tests/host/core/forwarder_plan_test.cpp`
- Produces:
  - `kSelfChannelLow = 0x554E4C41u`, `kSelfChannelTitleId = 0x00010001554E4C41ull`, `isSelfChannelTitle(u64)`.
  - `return_to = self` option resolving to `kSelfChannelLow` when `UNLA` is installed.
  - `forwarderCandidates()`, `forwarderArgv(dolPath)`, `hasForwarderFlag(argc, argv)`.
- [ ] Write failing unit tests:
  - `isSelfChannelTitle(0x00010001554E4C41ull)` is true; other channel IDs false.
  - `emunand` scanner skips `00010001/554e4c41` (`UNLA`) while keeping other channels.
  - `game_options` parses/serializes `return_to = self` and resolves to `0x554E4C41`.
  - `forwarderCandidates()` returns `["sd:/apps/newlauncher/boot.dol", "usb:/apps/newlauncher/boot.dol"]`; `forwarderArgv` appends `--from-forwarder`; `hasForwarderFlag` detects it.
- [ ] Implement core helpers, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add UNLA self-channel recognition and forwarder launch helpers`.

### Task 2 `[HOST]` `[BUILD]`: Priiloader `"Pune"` System Menu Exit, `nand_titles` Exclusion & Channel/Autostart UI
**Files:** Modify `src/platform/nand_titles.cpp`, `src/platform/system.{hpp,cpp}`, `src/ui/strings.{hpp,cpp}`, `src/ui/settings_scene.{hpp,cpp}`, `src/ui/app_command.hpp`, `src/app/main.cpp`; Create `src/ui/channel_scene.{hpp,cpp}`; Tests `tests/host/ui/channel_scene_test.cpp`
- [ ] Write failing UI tests for `ChannelScene` (DE + EN strings, shows launch source, `UNLA` NAND detection status, `forwarder.dol` presence, BootMii NAND backup warning, Priiloader step-by-step instructions, "Export Forwarder" action emits `AppCommand::ExportForwarder`).
- [ ] Implement `ChannelScene` + Settings entry.
- [ ] Update `src/platform/nand_titles.cpp` to skip `isSelfChannelTitle(ids[i])`.
- [ ] Update `src/platform/system.cpp` System Menu exit to write Priiloader magic `0x50756E65` (`"Pune"`) to `0x8132FFFB` (`DCFlushRange`), clear return stub params, log `NL_EXIT_MENU priiloader_magic=1`, and call `SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0)`.
- [ ] Wire `AppCommand::ExportForwarder` in `src/app/main.cpp` (copies embedded or app-folder `forwarder.dol` to `sd:/apps/newlauncher/forwarder.dol` with atomic write, logs `NL_CHAN_EXPORT ok|fail`, never touches NAND), and log `NL_START source=forwarder|hbc|game` + `NL_CHAN_UNLA installed=yes|no` at startup.
- [ ] Run `ctest --preset host` and Wii builds (`build_wii.py` & `--no-tls`) — verify 0 warnings.
- [ ] Commit: `feat(ui,platform): add Channel/Autostart helper and Priiloader-safe System Menu exit`.

### Task 3 `[BUILD]`: Standalone `forwarder.dol` Target (`forwarder/`)
**Files:** Create `forwarder/main.cpp`; Modify `CMakeLists.txt`, `tools/make_release.py`, `tools/tests/test_make_release.py`
- [ ] Add failing test in `tools/tests/test_make_release.py` asserting that `make_release.py` includes `apps/newlauncher/forwarder.dol` when built.
- [ ] Implement `forwarder/main.cpp`:
  - Preserves AHBPROT / IOS 58 without unnecessary reload.
  - Mounts SD; tries `sd:/apps/newlauncher/boot.dol`; if missing, tries USB (bounded 5 s retry) for `usb:/apps/newlauncher/boot.dol`.
  - Validates DOL via shared `src/core/dol.cpp` (`parseDol`), loads into `kDolBufferAddr` (`0x92800000`), prepares `__system_argv` (`<path>`, `--from-forwarder`), unmounts FAT/USB cleanly, copies embedded `nl_booter` to `kBooterAddr`, and jumps to `app_booter`.
  - Links above `0x80F00000` (and lowers ArenaHi2 below `kDolBufferAddr`) so it never collides with `newlauncher.dol`'s load addresses.
  - On missing/invalid DOL: displays bilingual DE/EN missing-file screen, logs `NL_FWD missing`, waits 5 s or button press, exits cleanly.
- [ ] Add CMake target `nl_forwarder` (`OUTPUT_NAME forwarder`, `ogc_create_dol(nl_forwarder)` → `build/wii/forwarder.dol`, 0 warnings) and embed/copy `forwarder.dol` in `tools/make_release.py` + `.github/workflows/build.yml` artifact upload.
- [ ] Run `python tools/build_wii.py`, `python tools/build_wii.py --no-tls`, `python tools/check_licenses.py build/wii`, and `python -m unittest discover -s tools/tests` — verify green & 0 warnings.
- [ ] Commit: `feat(forwarder): build standalone UNLA forwarder.dol and bundle in releases`.

### Task 4 `[HOST]`: Original Channel Assets & Local Wii/vWii WAD Staging Tool (`tools/build_channel.py`)
**Files:** Create `tools/gen_channel_assets.py`, `tools/build_channel.py`, `assets/channel/README.md`, `assets/channel/icon_src.png`, `assets/channel/banner_src.png`, `assets/channel/sound_src.wav`; Test `tools/tests/test_build_channel.py`
- [ ] Write failing unit tests in `tools/tests/test_build_channel.py`:
  - `assets` generator produces deterministic 100% original icon/banner/chime assets and valid `00000000.app` with `IMET` magic, localized titles (`newlauncher`), and verified IMET MD5 checksum.
  - `bundle --target wii` and `--target vwii` reject missing/corrupt DOL, write `UNLA` (`00010001554E4C41`) staging metadata, and enforce Wii vs. vWii target guard (refusing mismatched base NAND loader).
  - Verify `.gitignore` ignores `build/channel/` and `*.wad`.
- [ ] Implement `tools/gen_channel_assets.py` (generates original vector/pixel icon + banner PNGs and synthesized chime WAV under `assets/channel/` — 100% project-owned, zero third-party Nintendo assets) and `tools/build_channel.py`.
- [ ] Run `python -m unittest discover -s tools/tests -v` — verify green.
- [ ] Commit: `feat(tools): add original channel assets and local Wii/vWii channel bundler`.

### Task 5 `[DOLPHIN]`: Forwarder Dolphin Smokes & User Documentation
**Files:** Modify `tools/dolphin_smoke.py`, `tools/tests/test_dolphin_smoke.py`, `docs/user-guide.md`, `docs/STATUS.md`; Create `docs/channel-autostart.md`, `docs/acceptance/m30-wii.md`
- [ ] Add `--forwarder-smoke ok|missing` to `tools/dolphin_smoke.py`:
  - `ok`: boots `build/wii/forwarder.dol` in Dolphin with `apps/newlauncher/boot.dol` on virtual SD → verifies `NL_FWD boot=sd`, `NL_START source=forwarder`, and `NL_READY`.
  - `missing`: boots `build/wii/forwarder.dol` without `boot.dol` → verifies `NL_FWD missing` and clean exit.
- [ ] Run both Dolphin forwarder smokes — verify green.
- [ ] Write `docs/channel-autostart.md` (BootMii NAND backup guide, Priiloader autostart setup for Wii & vWii, local `UNLA` WAD packaging & YAWM installation steps, Wii vs. vWii brick warnings, exiting to System Menu) and `docs/acceptance/m30-wii.md` (for Stage 6 `[WII-HW]` pass).
- [ ] Commit: `test(m30): add Dolphin forwarder smokes and channel/autostart docs`.
- [ ] Open PR, wait for CI (`gh run watch <id> --exit-status`), merge once green.

### Deferred to Stage 6 `[WII-HW]` (Do NOT Run Yet)
- [ ] Execute `docs/acceptance/m30-wii.md` on real Wii / vWii during the consolidated Stage 6 hardware pass (only after explicit user confirmation and verified BootMii NAND backup).
