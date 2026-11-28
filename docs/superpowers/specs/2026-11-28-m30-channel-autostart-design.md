# M30: Wii System Menu Channel (`UNLA` Forwarder) & Autostart Design

Status: Draft Spec (2026-11-28) · Tracks: [Issue #5](https://github.com/pudding-pudding00/newlauncher/issues/5)

## 1. Purpose & Goals

Make **newlauncher** installable as a first-class channel on the Wii System Menu (and vWii on Wii U) with an optional power-on autostart flow, while preserving:

- **Automatic compatibility with Self-Update (M20):** Updating `sd:/apps/newlauncher/boot.dol` never requires rebuilding or reinstalling a NAND channel WAD.
- **Public repository & legal hygiene (`AGENTS.md`):** Zero committed `.wad` files, zero third-party Nintendo banner templates, and zero Wii Common Keys in source code.
- **Hardware safety:** Separate Wii vs. vWii packaging (to prevent vWii banner/NAND-loader bricks), never silently write to real Wii NAND, and always require a BootMii NAND backup warning before installation.
- **No autostart trap:** Users who enable autostart can always exit cleanly to the real Wii System Menu from `HomeMenu`.

## 2. Channel Identity (`UNLA`)

- **4-character ASCII ID:** `UNLA`
- **64-bit Title ID:** `0x00010001554E4C41` (`00010001-554E4C41`, channel type `00010001`, low 32-bit `0x554E4C41`).
- **Rationale:**
  - `U`: standard homebrew forwarder prefix; never collides with Nintendo Virtual Console or WiiWare prefixes (e.g., `N...` = N64 VC, `W...` = WiiWare, `F...` = NES VC, `J...` = SNES VC), ensuring `newlauncher`'s own VC/channel scanners never misclassify the channel as a game.
  - `NL`: **n**ew**l**auncher.
  - `A`: Region-Free / All regions.

### Launcher Awareness of `UNLA` (`src/core/` + `src/platform/`)

- Define constants in `src/core/wii_title.hpp`:
  - `inline constexpr u32 kSelfChannelLow = 0x554E4C41u;` (`"UNLA"`)
  - `inline constexpr u64 kSelfChannelTitleId = (u64{kChannelTitleType} << 32) | kSelfChannelLow;`
  - `bool isSelfChannelTitle(u64 titleId);`
- **Library Exclusion:** Both real-NAND channel scanning (`src/platform/nand_titles.cpp`) and EmuNAND scanning (`src/core/emunand.cpp`) skip `isSelfChannelTitle(tid)` so `newlauncher` never appears as a launchable game tile inside itself.
- **Return-to-Channel Support (`src/core/game_options.{hpp,cpp}`):**
  - Keep existing `return_to = hbc` (`LULZ`/`JODI`) and explicit 16-hex `00010001xxxxxxxx`.
  - Add `return_to = self` (or `newlauncher`) mapping to `kSelfChannelLow` (`UNLA`) when `UNLA` is present in NAND, falling back cleanly to the existing return stub (`src/launch/return_stub`) if `UNLA` is not installed.
- **Launch Source Detection (`src/core/launch_source.{hpp,cpp}`):**
  - Detect `--from-forwarder` in `argv` (passed by `forwarder.dol`) alongside existing `--from-game` and `--remote`. Log `NL_START source=forwarder|hbc|game`.

## 3. Forwarder DOL (`forwarder/`)

The forwarder is a small, standalone devkitPPC Wii executable (`build/wii/forwarder.dol`, target `nl_forwarder`) separate from `newlauncher.dol`.

### 3.1 Boot & Search Order

1. Initialize minimal console/video (silent black screen on fast path; draw minimal text only on fatal error).
2. Preserve AHBPROT (`*(vu32*)0xCD800064 & 1`):
   - If IOS 58 is already active with AHBPROT enabled, do **not** reload IOS (preserves hardware access for USB 2.0 / GameCube / ISFS).
   - Otherwise, if AHBPROT is disabled and IOS 58 is present, reload IOS 58 as fallback.
3. Probe storage in deterministic order:
   1. `sd:/apps/newlauncher/boot.dol`
   2. `usb:/apps/newlauncher/boot.dol` (with a bounded 5 s USB spin-up retry loop only if SD does not contain `boot.dol`)
4. Validate the target binary using shared `src/core/dol.hpp` (`parseDol`, `kMaxDolBytes`).
5. Load the DOL into MEM2 (`kDolBufferAddr = 0x92800000`), build the `__system_argv` block (`argv[0] = "<dev>:/apps/newlauncher/boot.dol"`, `argv[1] = "--from-forwarder"`), unmount FAT/USB cleanly, and transfer control via the freestanding `src/launch/app_booter` (`kBooterAddr = 0x93300000`).
6. **Memory Layout Safety:** Link `nl_forwarder` above `0x80F00000` (or lower ArenaHi2 below `kDolBufferAddr` just like `newlauncher`) so `forwarder.dol` never overlaps `newlauncher.dol`'s entry/code range (`0x80004000+`) during copy, and uses the exact same `app_booter` copy routine.
7. **Failure Path:** If neither SD nor USB contains a valid `boot.dol`, display a brief bilingual (DE/EN) on-screen message naming the searched paths (`sd:/apps/newlauncher/boot.dol`, `usb:/apps/newlauncher/boot.dol`), log `NL_FWD missing`, wait 5 seconds (or any button press), and exit cleanly to System Menu / HBC.

### 3.2 Pure Core Helpers (Host-Testable)

Put forwarder path/candidate selection and `argv` construction in `src/core/forwarder_plan.{hpp,cpp}` (zero `<ogc/...>`):

- `std::vector<std::string> forwarderCandidates();` (`sd:/apps/newlauncher/boot.dol`, `usb:/apps/newlauncher/boot.dol`)
- `std::vector<std::string> forwarderArgv(std::string_view dolPath);`
- `bool hasForwarderFlag(int argc, const char* const* argv);`

## 4. Original Channel Assets & Local WAD Packaging

### 4.1 Asset Rules

- Create 100% original visual & audio assets under `assets/channel/`:
  - `icon_src.png` (128×96 RGBA, Wii-style rounded tile matching newlauncher's light/dark accent motif)
  - `banner_src.png` (590×332 RGBA, clean channel banner artwork with "newlauncher" wordmark)
  - `sound_src.wav` (short original synthesized chime generated deterministically by a Python helper, e.g., 2–3 s, 32 kHz mono/stereo PCM → BNS/ADPCM or WAV)
- **Hard Rules:**
  - Never commit `.wad` files, decrypted Nintendo channel dumps, third-party `.bnr` files, or the Wii Common Key.
  - All generated channel archives (`banner.bin`, `icon.bin`, `00000000.app`, `.wad`) live under `build/channel/` (gitignored).

### 4.2 Local Build Tooling (`tools/build_channel.py`)

Provide a host-tested Python tool `tools/build_channel.py`:

- Subcommand `assets`: converts `assets/channel/*` into deterministic U8/TPL/IMET banner metadata (`00000000.app`) with localized IMET titles (`newlauncher` in all Wii language slots, valid IMET MD5 header).
- Subcommand `bundle --target wii|vwii --dol build/wii/forwarder.dol --out build/channel/`:
  - Verifies `forwarder.dol` exists and parses cleanly as a valid DOL.
  - Emits an unsigned channel staging directory (`title_id.txt` = `UNLA` / `00010001554E4C41`, `00000000.app`, `forwarder.dol`, `README.txt` with exact local WAD packing & installation steps for the selected `--target`).
  - Optional `--pack-wad` flag invokes an external user-installed WAD packer (e.g., `CustomizeMii` / `ModMii` / `wadpacker` with user-supplied local key/base) ONLY on the user's machine; refuses to proceed if `--target` does not match the supplied base NAND loader (Wii vs. vWii guard).
- **Release Packaging (`tools/make_release.py`):**
  - Include `apps/newlauncher/forwarder.dol` in the release ZIP alongside `boot.dol` so Priiloader users have it immediately on SD without extra downloads.

## 5. Autostart via Priiloader & System Menu Exit

### 5.1 Priiloader Setup Flow (Wii & vWii)

Because the Wii System Menu has no built-in autostart toggle, autostart is provided cleanly via **Priiloader** (which supports both Wii and vWii):

1. User ensures a fresh **BootMii NAND backup** exists (documented prominently in `docs/channel-autostart.md` and in the in-app info dialog).
2. In-app helper under **Settings → System → Channel & Autostart** (`ui/channel_scene.{hpp,cpp}` or section in `settings_scene`):
   - Shows current launch source (HBC / Forwarder / Priiloader), whether `UNLA` channel is detected in NAND, and whether `sd:/apps/newlauncher/forwarder.dol` is present.
   - Action **"Export Forwarder for Priiloader"**: copies `forwarder.dol` to `sd:/apps/newlauncher/forwarder.dol` (and optionally `sd:/apps/priiloader/forwarder.dol` or `sd:/forwarder.dol` if requested, without touching NAND), logging `NL_CHAN_EXPORT ok|fail`.
   - Displays concise step-by-step DE/EN instructions for Priiloader:
     1. Hold `RESET` at power-on to enter Priiloader.
     2. Select **Load/Install File** → install `forwarder.dol`.
     3. Under **Settings**, set **Autoboot: Installed File** (and optionally **Return to: Autoboot**).
     4. Save settings.

### 5.2 Avoiding the Autostart Loop (`HomeMenu` → Wii Menu)

When Priiloader is set to `Return to: Autoboot`, a plain `SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0)` loops right back into `newlauncher`!

- **Spec Requirement:** In `src/platform/system.cpp`, the **"Wii Menu"** action in `HomeMenu` writes the standard Priiloader System Menu magic (`0x50756E65`, `"Pune"`) to `0x8132FFFB` (flushed with `DCFlushRange`) immediately before calling `SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0)`.
- On consoles without Priiloader, `0x8132FFFB` is unused by the System Menu and harmless; on consoles with Priiloader `Return to: Autoboot`, Priiloader sees `"Pune"` and boots the real Wii System Menu.
- Keep `return_stub` cleared (`clearReturnStubParams()`) on System Menu exit so the return stub does not intercept it.

## 6. UI, Strings & Logging

- All new UI labels, helper instructions, and BootMii warnings exist in both **German and English** (`src/ui/strings.hpp`).
- Log markers:
  - `NL_START source=forwarder|hbc|game`
  - `NL_FWD boot=<dev>` / `NL_FWD missing`
  - `NL_CHAN_UNLA installed=yes|no`
  - `NL_CHAN_EXPORT ok|fail`
  - `NL_EXIT_MENU priiloader_magic=1`

## 7. Verification Strategy (No-Wii-First)

1. **`[HOST]`:** Unit tests for `isSelfChannelTitle`, `emunand` skipping `UNLA`, `return_to = self`, `forwarder_plan` candidates/argv/flag parsing, `tools/tests/test_build_channel.py` (IMET MD5, U8/TPL generation, Wii vs. vWii target guard), and `ui` tests for `HomeMenu` & Settings Channel/Autostart UI.
2. **`[BUILD]`:** `nl_forwarder` (`forwarder.dol`) compiles with 0 warnings in both TLS and `--no-tls` builds; `tools/make_release.py` includes `forwarder.dol`; `tools/check_licenses.py` passes.
3. **`[DOLPHIN]`:** Dolphin smoke boots `forwarder.dol` with a virtual SD containing `apps/newlauncher/boot.dol` (or `argv_echo.dol`) and verifies `NL_FWD` → handoff with `--from-forwarder` and `NL_READY` (plus missing-file error path smoke).
4. **`[WII-HW]` (Final Hardware Phase Only):** `docs/acceptance/m30-wii.md` — real Wii/vWii checks after explicit user confirmation and BootMii NAND backup: Priiloader autoboot with `forwarder.dol`, `HomeMenu` → real Wii Menu via `"Pune"` magic, local WAD channel launch (`UNLA`), self-update persistence without reinstalling channel, and return from Wii/homebrew back to launcher.
