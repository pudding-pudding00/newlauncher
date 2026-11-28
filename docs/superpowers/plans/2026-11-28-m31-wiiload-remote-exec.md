# M31: Remote Software Loading & Execution (`wiiload` v0.5) — Implementation Plan

> **For agentic workers:** Follow TDD (failing test first, then implementation). Complete **all `[HOST]`, `[BUILD]`, and `[DOLPHIN]` tasks first**; defer `[WII-HW]` to the consolidated Stage 6 hardware pass in [`2026-11-28-m30-m34-master-roadmap.md`](2026-11-28-m30-m34-master-roadmap.md).

**Goal:** Add an opt-in, standards-compliant **`wiiload` v0.5** TCP receiver (port `4299`) supporting **`.dol`**, **`.elf`**, and **`.zip`** (`apps/<name>/`) payloads, with confirmation UI, session sender trust, Parental Controls PIN enforcement, and automatic return to **newlauncher** via `return_stub`.

**Spec:** [`docs/superpowers/specs/2026-11-28-m31-wiiload-remote-exec-design.md`](../specs/2026-11-28-m31-wiiload-remote-exec-design.md)

**Branch:** `feat/m31-wiiload`

## Global Constraints
- Platform-free protocol/ELF/ZIP logic lives strictly in `src/core/` (no `<ogc/...>`, no `throw`, no RTTI).
- Plain libogc sockets in `src/platform/wiiload_server.{hpp,cpp}` — never call `wtls_*`.
- 0 warnings on Wii builds with and without TLS; bilingual DE + EN strings in `src/ui/strings.hpp`.
- Default `[network] wiiload = off` (opt-in); Parental PIN overrides `auto` when active.

---

### Task 1 `[HOST]`: `wiiload` v0.5 Wire Protocol Parser & Format Detector
**Files:** Create `src/core/wiiload_proto.{hpp,cpp}`; Update `CMakeLists.txt`; Test `tests/host/core/wiiload_proto_test.cpp`
- Produces:
  - `enum class WiiloadKind : u8 { Dol, Elf, Zip };`
  - `struct WiiloadHeader { u8 major, minor; u16 argsLen; u32 compSize; u32 uncompSize; };`
  - `Result<WiiloadHeader> parseWiiloadHeader(std::span<const u8> hdr);`
  - `Result<std::vector<u8>> decodeWiiloadPayload(const WiiloadHeader&, std::span<const u8> raw);` (raw when `uncompSize == 0`, zlib via `core/inflate` otherwise)
  - `std::vector<std::string> parseWiiloadArgs(std::span<const u8> rawArgs);`
  - `WiiloadKind detectWiiloadKind(std::span<const u8> payload, std::string_view argv0);`
- [ ] Write failing unit tests: valid v0.5 & v0.4 headers, raw payload, zlib-compressed payload (deflated via `core/deflate` / zlib header), bad magic, bad version, zero/oversize `compSize`, oversize `uncompSize`, mismatched inflated length, corrupt zlib stream, `argv` splitting (empty, single, multi, trailing NUL, > `kWiiloadMaxArgs`), magic-based detection (`PK\x03\x04` → Zip, `\x7FELF` → Elf, else Dol).
- [ ] Implement `src/core/wiiload_proto.{hpp,cpp}`, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add wiiload v0.5 protocol parser and format detector`.

### Task 2 `[HOST]`: 32-Bit PowerPC ELF Validator & In-Memory DOL Converter
**Files:** Create `src/core/elf.{hpp,cpp}`; Update `CMakeLists.txt`; Test `tests/host/core/elf_test.cpp`
- Produces:
  - `bool isElfMagic(std::span<const u8> data);`
  - `Result<std::vector<u8>> elfToDol(std::span<const u8> elf, u32 maxDolBytes = kMaxDolBytes);`
- [ ] Write failing unit tests:
  - Synthetic valid 32-bit BE PPC `ET_EXEC` ELF with `PF_X` text segment, `PF_R|PF_W` data segment, and BSS (`p_memsz > p_filesz`) converts cleanly and output passes `checkDolImage(..., kMaxDolBytes)` with matching entry point & section contents.
  - Physical-to-cached address normalization (`0x00004000` → `0x80004000`).
  - Rejects: little-endian (`EI_DATA=1`), 64-bit (`EI_CLASS=2`), non-PPC (`e_machine!=20`), `ET_REL`/`ET_DYN`, truncated phdrs/segments, `p_filesz > p_memsz`, > 7 text or > 11 data `PT_LOAD` segments, overlapping segments, out-of-bounds MEM1/MEM2, overlap with `[kDolBufferAddr, kReservedEnd)`, bad entry point, output exceeding `maxDolBytes`.
- [ ] Implement `src/core/elf.{hpp,cpp}`, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add 32-bit PowerPC ELF validator and DOL converter`.

### Task 3 `[HOST]`: Safe `.zip` Homebrew Package Inspector & Atomic SD Installer
**Files:** Create `src/core/wiiload_zip.{hpp,cpp}`; Update `CMakeLists.txt`; Test `tests/host/core/wiiload_zip_test.cpp`
- Produces:
  - `struct WiiloadZipPlan { std::string appName; std::string entryPath; WiiloadKind entryKind; bool isBundle; u64 totalUnzipped; };`
  - `Result<WiiloadZipPlan> inspectWiiloadZip(std::span<const u8> zipBytes);`
  - `Result<std::string> installWiiloadZip(FileSystem& fs, std::span<const u8> zipBytes, const WiiloadZipPlan& plan, std::string_view root = "sd:");` (atomic `.tmp` → swap with `.old` rollback)
  - `Result<std::vector<u8>> extractWiiloadZipEntry(std::span<const u8> zipBytes, std::string_view entryPath);`
- [ ] Write failing unit tests (using in-memory `ZipWriter` + mock `FileSystem`):
  - Standard `apps/demo/boot.dol` bundle → `isBundle = true`, `appName = "demo"`, atomic install creates `sd:/apps/demo/boot.dol` + assets and leaves no `.tmp`/`.old`.
  - Standard `apps/demo/boot.elf` bundle → `entryKind = Elf`.
  - Root single-binary `boot.dol` / `boot.elf` → `isBundle = false`, extractable directly to RAM without touching SD.
  - Rejects: path traversal (`../evil`), absolute paths, missing `boot.dol`/`boot.elf`, nested invalid app names, total unzipped size > 32 MiB, rollback restores existing `sd:/apps/demo/` if write fails mid-extraction.
- [ ] Implement `src/core/wiiload_zip.{hpp,cpp}`, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add wiiload ZIP bundle inspector and atomic installer`.

### Task 4 `[HOST]`: Config Keys, Settings Model & Confirmation UI Scene
**Files:** Modify `src/core/config.{hpp,cpp}`, `src/core/settings_model.{hpp,cpp}`, `src/ui/strings.{hpp,cpp}`, `src/ui/app_command.hpp`, `src/ui/settings_scene.{hpp,cpp}`; Create `src/ui/wiiload_scene.{hpp,cpp}`; Tests `tests/host/core/config_test.cpp`, `tests/host/ui/wiiload_scene_test.cpp`
- [ ] Write failing tests for:
  - `[network] wiiload = off|confirm|auto` (default `off`, invalid falls back to `off` + `badKeys`), `[debug] wiiload` & `[debug] wiiload_port` (debug-only).
  - `WiiloadScene`: displays sender IP, payload kind (`DOL`/`ELF`/`ZIP`), filename, formatted size, `argv` summary, target SD path for `.zip` bundles; buttons emit `WiiloadAccept`, `WiiloadTrustSender`, `WiiloadDecline` (also on `B`); DE + EN strings complete.
  - Parental Controls PIN gate tested on settings change and incoming execution.
- [ ] Implement config, settings entry, bilingual strings, and `WiiloadScene`; verify `ctest --preset host` green.
- [ ] Commit: `feat(ui): add wiiload setting and incoming payload confirmation scene`.

### Task 5 `[BUILD]`: Platform `WiiloadServer` Thread & `main.cpp` Integration
**Files:** Create `src/platform/wiiload_server.{hpp,cpp}`; Modify `src/app/main.cpp`, `src/launch/chainloader.{hpp,cpp}` (add `loadDolBytesToBuffer(std::span<const u8>)` if needed), `CMakeLists.txt`
- [ ] Implement `platform::WiiloadServer` (LWP thread, 64 KiB stack, plain `net_*` sockets, port `4299` / debug override, 10 s header timeout, 30 s payload timeout, 16 KiB read chunks, off-thread inflate + kind detection, mutex-protected `take()`, clean `stop()` + rebind on Wi-Fi reconnect).
- [ ] Wire in `src/app/main.cpp`:
  - Start/stop `WiiloadServer` according to effective mode (`off` → stopped; `confirm`/`auto` → listening).
  - Maintain in-memory session-trusted sender IP set (cleared on exit).
  - On incoming payload: apply Parental PIN gate if locked; show `WiiloadScene` unless `auto` or trusted sender; on accept:
    - `.dol` → validate & load to `kDolBufferAddr`
    - `.elf` → `elfToDol()` & load to `kDolBufferAddr`
    - `.zip` → if bundle: `installWiiloadZip()` then load `boot.dol` (or `elfToDol(boot.elf)`); if single-binary: extract & load to `kDolBufferAddr`
  - Stop all network servers (`WiiloadServer`, `RemoteServer`, `PairingServer`), `NetService`, audio, workers; call `launch::installReturnStub(selfDol)` before `unmountAll()`; call `launch::chainload(plan)`.
  - Emit all `NL_WIILOAD ...` markers.
- [ ] Build Wii targets: `python tools/build_wii.py` AND `python tools/build_wii.py --no-tls` — verify **0 warnings** on both.
- [ ] Commit: `feat(platform): add wiiload TCP receiver and DOL/ELF/ZIP execution path`.

### Task 6 `[HOST]` `[DOLPHIN]`: Dolphin End-to-End Smokes (`.dol`, `.elf`, `.zip`) & Docs
**Files:** Modify `tools/dolphin_smoke.py`, `tools/tests/test_dolphin_smoke.py`, `docs/user-guide.md`, `docs/STATUS.md`; Create `docs/acceptance/m31-wii.md`
- [ ] Add Python helper & unit tests in `tools/tests/test_dolphin_smoke.py` for crafting `wiiload` v0.5 packets (`.dol`, `.elf`, `.zip`, zlib-compressed & uncompressed).
- [ ] Extend `tools/dolphin_smoke.py` with `--wiiload-smoke dol|elf|zip`:
  - Boots `newlauncher.dol` with `[debug] wiiload=auto`, waits for `NL_WIILOAD listen`.
  - Sends real compiled `build/wii/argv_echo.dol`, real compiled `build/wii/argv_echo.elf`, and a dynamically built `apps/echo_test/boot.dol` `.zip` over `127.0.0.1:4299` with custom args.
  - Verifies `NL_WIILOAD exec`, `argv_echo` printing expected `argv`, and (for `.zip`) verifies `apps/echo_test/boot.dol` exists on virtual SD.
  - Also test invalid magic / corrupt payload → `NL_WIILOAD reject` without crashing launcher.
- [ ] Run all 3 Dolphin smokes (`dol`, `elf`, `zip`) + `python -m unittest discover -s tools/tests` — verify green.
- [ ] Write `docs/acceptance/m31-wii.md` (for Stage 6 `[WII-HW]` pass) and update `docs/user-guide.md`.
- [ ] Commit: `test(m31): add Dolphin wiiload smokes for DOL, ELF, and ZIP`.
- [ ] Open PR, wait for CI (`gh run watch <id> --exit-status`), merge once green.

### Deferred to Stage 6 `[WII-HW]` (Do NOT Run Yet)
- [ ] Execute `docs/acceptance/m31-wii.md` on real Wii during the consolidated Stage 6 hardware pass.
