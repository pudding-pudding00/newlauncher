# M31: Remote Software Loading & Execution (`wiiload` v0.5) Design

Status: Draft Spec (2026-11-28) · Tracks: [Issue #5](https://github.com/pudding-pudding00/newlauncher/issues/5)

## 1. Purpose & Goals

When **newlauncher** runs as the primary System Menu channel or Priiloader autostart frontend, the Homebrew Channel (HBC) is no longer running. Users and developers still need to push and execute Wii homebrew over the local network without swapping SD cards.

M31 embeds a standards-compliant **`wiiload` v0.5** TCP receiver (port `4299`) supporting **all three standard formats**:

1. **`.dol`** — validated and executed directly from RAM.
2. **`.elf`** — validated 32-bit big-endian PowerPC ELF converted in memory to a DOL image and executed directly from RAM.
3. **`.zip`** — validated homebrew archive (`apps/<name>/boot.dol` or `boot.elf`), extracted safely to `sd:/apps/<name>/` and executed (or executed directly for single-binary archives).

Exiting the remotely launched homebrew returns cleanly back to **newlauncher** via the existing `src/launch/return_stub`.

## 2. Wire Protocol (`wiiload` v0.5)

TCP listener on port **`4299`** (`kWiiloadPort = 4299`, configurable only via `[debug] wiiload_port` for parallel/host tests).

### 2.1 Packet Layout

| Offset | Size | Field | Details |
|---|---|---|---|
| `0` | 4 | Magic | ASCII `"HAXX"` (`0x48 0x41 0x58 0x58`) |
| `4` | 1 | Major version | `0` |
| `5` | 1 | Minor version | `5` (also tolerate `4` with `args_len = 0`) |
| `6` | 2 | `args_len` | Big-endian `u16`; byte length of trailing NUL-separated `argv` block (`<= 4096`) |
| `8` | 4 | `compressed_size` | Big-endian `u32`; exact bytes of payload following the 16-byte header (`> 0`) |
| `12` | 4 | `uncompressed_size` | Big-endian `u32`; `0` = raw uncompressed payload; `> 0` = zlib-compressed payload whose inflated size must equal `uncompressed_size` |
| `16` | `compressed_size` | Payload | Raw or zlib-stream bytes |
| `16 + compressed_size` | `args_len` | Arguments | NUL-separated `argv` strings (`argv[0]` is conventionally the sent filename, e.g. `app.dol`, `app.elf`, `pkg.zip`) |

### 2.2 Limits & Rejection Rules (`src/core/wiiload_proto.{hpp,cpp}`)

- `kWiiloadHeaderBytes = 16`
- `kWiiloadMaxArgs = 4096`
- `kWiiloadMaxCompBytes = 16u * 1024 * 1024` (16 MiB)
- `kWiiloadMaxUncompDolElf = kMaxDolBytes` (11 MiB, matching `core/launch_layout.hpp`)
- `kWiiloadMaxUncompZip = 16u * 1024 * 1024` (16 MiB compressed archive; max 32 MiB total extracted size)
- Reject immediately (close connection, log `NL_WIILOAD reject reason=<code_name>`) on:
  - Bad magic or unsupported version (`major != 0 || (minor != 4 && minor != 5)`)
  - `compressed_size == 0` or `> kWiiloadMaxCompBytes`
  - `uncompressed_size > kWiiloadMaxUncompZip`
  - `args_len > kWiiloadMaxArgs`
  - Zlib inflate failure or inflated length != `uncompressed_size` (reusing `src/core/inflate.hpp`)

### 2.3 Format Detection (`WiiloadKind`)

Detect payload kind from magic bytes first, using `argv[0]` extension only as secondary hint:

1. Starts with `PK\x03\x04` → `WiiloadKind::Zip`
2. Starts with `\x7FELF` → `WiiloadKind::Elf`
3. Otherwise → `WiiloadKind::Dol` (must pass `checkDolImage`)

## 3. Format Handlers (Platform-Free in `src/core/`)

### 3.1 `.dol` Handler

- Validated directly via `checkDolImage(payload, kMaxDolBytes)` (`src/core/dol.hpp`).
- Copied to `kDolBufferAddr` (`0x92800000`) and booted via `launch::chainload()`.

### 3.2 `.elf` Handler (`src/core/elf.{hpp,cpp}`)

New host-testable 32-bit PowerPC ELF validator & DOL converter:

- Validate ELF32 header:
  - Magic `\x7FELF`, `EI_CLASS == 1` (32-bit), `EI_DATA == 2` (big-endian), `e_type == 2` (`ET_EXEC`), `e_machine == 20` (`EM_PPC`), `e_phentsize >= 32`, program header table within payload bounds.
  - Entry point `e_entry` within valid MEM1/MEM2 executable range (`[kMem1Begin, kMem1End)` or `[kMem2Begin, kDolBufferAddr)`).
- Inspect `PT_LOAD` program headers (`p_type == 1`, `p_memsz > 0`):
  - Require `p_filesz <= p_memsz`, `p_offset + p_filesz <= payload.size()`, target virtual/physical address (`p_paddr` if non-zero else `p_vaddr`, converted to cached `0x80000000`/`0x90000000` range if physical) completely inside `[kMem1Begin, kMem1End)` or `[kMem2Begin, kDolBufferAddr)`.
  - Reject any segment overlapping `[kDolBufferAddr, kReservedEnd)` or overlapping another `PT_LOAD` segment.
  - Map `PF_X` segments into DOL text slots (`0..6`, max 7) and non-`PF_X` segments with `p_filesz > 0` into DOL data slots (`7..17`, max 11).
  - Handle BSS (`p_memsz > p_filesz`):
    - If a single contiguous BSS range sits after the initialized data, encode as `DolHeader::bssAddress` / `bssSize`.
    - For multiple disjoint zero-fill tails, zero-pad within the emitted DOL section up to a safe cap (or compute minimal bounding BSS if non-overlapping) so `app_booter` zeroes BSS before copying initialized sections (matching `booter.c` lines 47–55).
- Output: `Result<std::vector<u8>> elfToDol(std::span<const u8> elf, u32 maxDolBytes);` whose output is guaranteed to pass `checkDolImage(..., maxDolBytes)`.

### 3.3 `.zip` Handler (`src/core/wiiload_zip.{hpp,cpp}`)

Reuses `src/core/zip_reader.hpp` (`readZipDirectory`, `isSafeZipPath`, `extractZipEntry`) and `src/core/emu_install.hpp` staging discipline:

- Validate all entries with `isSafeZipPath` (no `..`, no leading `/`, no `\`, no `:`, no control chars), entry count `<= kMaxZipEntries`, total uncompressed size `<= 32 MiB`.
- Locate launch target inside the archive:
  1. First match of `apps/<appname>/boot.dol` or `apps/<appname>/boot.elf` (preferred standard HBC bundle layout; `<appname>` must be a valid single directory segment).
  2. Fallback: root `boot.dol` or `boot.elf` (single-binary archive).
- **Install & Run flow (standard `apps/<appname>/...` package):**
  - Stage extraction to `sd:/apps/<appname>.tmp/` (checking free space first), then atomically swap into `sd:/apps/<appname>/` (keeping `.old` backup until swap completes, deleting on success, rolling back on failure).
  - Launch the installed `sd:/apps/<appname>/boot.dol` (or convert `boot.elf` via `elfToDol` and launch from RAM) with `argv[0] = "sd:/apps/<appname>/boot.dol"` (or `.elf`) followed by any extra `wiiload` args (`argv[1..]`).
- **Single-binary `.zip` (only `boot.dol` or `boot.elf` at root):**
  - Can run directly from RAM without writing to SD (or optionally save to SD if user picks Install & Run when `apps/<name>/` layout is present).

## 4. Platform Receiver (`src/platform/wiiload_server.{hpp,cpp}`)

- Mirrors `src/platform/remote_server.{hpp,cpp}` architecture:
  - Dedicated LWP thread (`kStackBytes = 64 * 1024`, priority below main thread), plain libogc sockets (`net_socket`, `net_bind`, `net_listen`, `net_poll`, `net_accept`, `net_recv`) — **zero `wtls_*` calls**.
  - Waits until `NetService` reports network up; closes & rebinds cleanly if Wi-Fi reconnects.
  - Handles one client connection at a time; reads header (10 s timeout), then payload + args (30 s timeout, 16 KiB chunks, checking `stop_` every slice).
  - Performs inflate + basic validation off the UI thread, then hands a single `WiiloadIncoming` package (sender IPv4 string, kind, filename, payload bytes, argv vector) to the main thread via mutex-protected `take()`.
  - `stop()` closes listen/client sockets and joins the thread cleanly before `chainload()` and before `NetService::shutdown()`.
- Coexistence:
  - Runs on port `4299`, never conflicting with `RemoteServer` (`4300`) or `PairingServer` (`4301`).
  - Does not conflict with HBC when launched from HBC (HBC closes its socket before chainloading newlauncher).

## 5. Security, Config & UX

### 5.1 Configuration (`src/core/config.{hpp,cpp}`)

- Section `[network]`:
  - `wiiload = off | confirm | auto` (default: **`off`** so no listening socket opens unless the user opts in).
- Section `[debug]` (Dolphin smokes only):
  - `wiiload = off | confirm | auto` (overrides `[network] wiiload` for smoke runs).
  - `wiiload_port = <u16>` (default `4299`).

### 5.2 Confirmation Dialog & Session Trust (`src/ui/wiiload_scene.{hpp,cpp}`)

- When `mode == Confirm` (and sender IP is not in the in-memory session trust set):
  - Push modal overlay displaying:
    - Sender IP (`<ip>`)
    - Payload kind (`DOL` / `ELF` / `ZIP`), filename (`argv[0]`), size (KiB / MiB), and extra arguments (if any)
    - For `.zip` with `apps/<name>/`: target install path `sd:/apps/<name>/`
  - Actions:
    - **Run** (or **Install & Run** for `.zip`)
    - **Run & Trust this PC (session only)** — adds sender IP to an in-memory `std::vector<u32>` cleared on exit
    - **Decline** (also `B` button) — discards payload cleanly
- **Parental Controls (`src/core/parental.hpp`):**
  - Changing `[network] wiiload` in Settings requires PIN when parental lock is active.
  - Incoming `wiiload` execution requires PIN before running when parental lock is active (even if `auto` is configured, parental lock overrides to require confirmation + PIN).

### 5.3 Execution & Return Stub

1. Stop `WiiloadServer`, `RemoteServer`, `PairingServer`, `NetService`, audio/music, and background workers.
2. Call `launch::installReturnStub(selfDol)` (preserving `--remote` if active) before `platform::unmountAll()`.
3. Load DOL bytes (direct `.dol`, converted `.elf`, or extracted `boot.dol`/`boot.elf`) into `kDolBufferAddr` and invoke `launch::chainload(plan)`.
4. When the launched homebrew calls `exit()`, the return stub reloads `newlauncher`.

## 6. Log Markers & Bilingual Strings

- Strings in `src/ui/strings.hpp` in both **German and English**.
- Log markers (no private tokens/paths leaked in public docs; sender IP logged only locally as marker or omitted if privacy-sensitive — log `NL_WIILOAD recv kind=dol|elf|zip size=<n> args=<k>` without raw LAN IP in public smoke fixtures):
  - `NL_WIILOAD listen port=<p>`
  - `NL_WIILOAD recv kind=dol|elf|zip size=<bytes> args=<count>`
  - `NL_WIILOAD reject reason=<reason>`
  - `NL_WIILOAD install app=<name> ok|fail`
  - `NL_WIILOAD exec kind=dol|elf|zip`
  - `NL_WIILOAD decline`
  - `NL_WIILOAD stop`

## 7. Verification Strategy (No-Wii-First)

1. **`[HOST]`:**
   - `tests/host/core/wiiload_proto_test.cpp`: HAXX header parsing (v0.5 & v0.4), raw vs. zlib payload, bad magic/version/oversize/corrupt zlib, `argv` splitting, magic-based kind detection.
   - `tests/host/core/elf_test.cpp`: valid synthetic 32-bit BE PPC ELF (text + data + BSS) → `elfToDol()` → passes `checkDolImage()`; rejects little-endian, 64-bit, non-PPC, overlapping segments, out-of-bounds MEM1/MEM2, reserved `kDolBufferAddr` overlap, truncated files.
   - `tests/host/core/wiiload_zip_test.cpp`: `apps/demo/boot.dol`, `apps/demo/boot.elf`, root `boot.dol`, path traversal (`../evil`), oversize archive, atomic staging & rollback on write failure.
   - `tests/host/ui/wiiload_scene_test.cpp`: DE+EN strings, Confirm/Trust/Decline flow, Parental PIN gate.
2. **`[BUILD]`:** 0 warnings on Wii builds with TLS and `--no-tls`, plus `argv_echo.elf` and `argv_echo.dol` test targets available for smokes.
3. **`[DOLPHIN]`:** Extend `tools/dolphin_smoke.py` (`--wiiload-smoke dol|elf|zip`):
   - Launches `newlauncher` in Dolphin with `[debug] wiiload=auto`, waits for `NL_WIILOAD listen`.
   - Sends real compiled `argv_echo.dol`, real compiled `argv_echo.elf`, and a packaged `apps/echo_test/boot.dol` `.zip` over `127.0.0.1:4299` using the `wiiload` v0.5 wire format.
   - Verifies `NL_WIILOAD exec`, `argv_echo` output (confirming `argv` delivery!), and for `.zip` verifies files extracted to virtual SD.
4. **`[WII-HW]` (Final Hardware Phase Only):** `docs/acceptance/m31-wii.md` — real Wii push via `python tools/wii_deploy.py --wiiload <wii-ip>` and upstream `wiiload` CLI for `.dol`, `.elf`, and `.zip`, plus verifying `exit()` returns to `newlauncher` via `return_stub`.
