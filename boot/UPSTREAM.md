# Game booter: origin

`boot/` is the game booter from **WiiFlow Lite**. It runs after the launcher at `0x80A80000`, reads the start
parameters (`the_CFG`) and the fragment list, sets up the cIOS disc emulation, runs the game's apploader, applies
the standard patches and jumps into the game.

| | |
|---|---|
| Repository | https://github.com/Fledge68/WiiFlow_Lite |
| Path | `resources/wiiflow_game_booter/` |
| Commit | `69c0d4d61984f4ed6fca51b4520320cadfac960d` (2026-08-19, `master`) |
| Authors | FIX94, Fledge68 and the WiiFlow contributors. Individual files by Nuke, WiiPower, The Lemon Man, Nicksasa, Hector Martin, Andre Heider, Dimok, blackb0x, crediar and others (see file headers) |
| License | GPLv3 or later (WiiFlow Lite), individual files GPLv2 or GPLv2 or later; see "Licenses" |

## Files

Taken: all files from `source/`, unchanged except for the list below.

| File | Content |
|---|---|
| `main.cpp` | flow: read CFG, open disc, apploader, patches, jump |
| `apploader.c/h`, `disc.c/h`, `wdvd.c/h`, `fst.c/h` | apploader, disc access through the cIOS `/dev/di`, fragment list to the cIOS |
| `patchcode.c/h`, `videopatch.c/h`, `kirbypatch.c/h`, `patchhook.S`, `multidol.c/h`, `wip.c/h` | game patches (video, language, hooks, cheats since M15) |
| `cios.c/h` | IOS information (`CurrentIOS` comes from `the_CFG`) |
| `ChannelHandler.cpp/hpp`, `fs.c/h` (LZ77 through `nl/lz77_bridge` since M11) | channel start (`TYPE_CHANNEL`). Unused, but `main.cpp` branches there; removing it would have changed `main.cpp` more than keeping it costs. |
| `gecko.c/h`, `memory.h`, `utils.h`, `types.h`, `frag.h`, `Config.hpp` | helpers and types |

Left out: only the `Makefile` (replaced by the CMake target `nl_game_booter` in `CMakeLists.txt`).

New in newlauncher:

- `include/booter_cfg.h`: hand-over format shared by launcher and booter (see below)
- `include/booter_defines.h`: replacement for WiiFlow's `source/defines.h`
- `nl/exception_close.cpp`: replacement for `__exception_closeall()` from old libogc
- `nl/progress.c/h`, `nl/lz77_bridge.*`: replacements for GPLv2-only files (M11, see "Licenses")

## Changes

| File | Change | Reason |
|---|---|---|
| `main.cpp:58` | `memcpy(&normalCFG, (void*)NL_WII_CFG_ADDR, …)` instead of `memcpy(&normalCFG, (void*)*EXT_ADDR_CFG, …)` | The launcher places `the_CFG` at the fixed address `0x93200000`. `0x90100000` lies in the launcher's MEM2 heap; writing there before teardown could destroy malloc metadata (plan M9). |
| `Config.hpp`, `types.h`, `frag.h`, `cios.h`, `wip.h` | The definitions of `the_CFG`, the `TYPE_*`/`IOS_TYPE_*` enums, `MAX_FRAG`/`Fragment`/`FragList`, `IOS_Info` and `WIP_Code` are verbatim in `include/booter_cfg.h`; the headers include it. | One definition for launcher and booter. A `static_assert` pins the layout (121 bytes and 240,012 bytes). |
| `patchcode.c:31` | `#include "booter_defines.h"` instead of `#include "../../../source/defines.h"` | The booter no longer lives in the WiiFlow tree. Only `APP_NAME`/`APP_VERSION` are needed for the patcher identification sent to Wiimmfi; the launcher identifies itself honestly as `newlauncher <version>`. |
| `wdvd.c` `WDVD_SetFragList` | If `IOS_Ioctl` returns `1`, the cIOS return value from `outbuf[0]` is passed on (upstream: always `0`). | d2x reports `Frag_Init` errors (e.g. `-4` = `usbstorage_Init` failed) only in `outbuf[0]`. Upstream treated this as success and ran into a black screen (M20, R6). |
| `main.cpp` (Wii game branch) | Every step (`WDVD_Init`, `Disc_SetUSB`, `Disc_Open`, `Disc_FindPartition`, `WDVD_OpenPartition`) is checked; on an error or `AppEntrypoint == 0` the booter shows a message (German and English, with a USB port 0 hint for `-4`) including return values, stores it as a crash report (`NLCR`, `0x932F0000`) in MEM2 and returns after RESET. | Upstream jumped to `AppEntrypoint = 0` even after errors: a permanent black screen without a hint (M20, R6). |
| `patchcode.c/h` `PatchReturnTo`, `returnToStub` | If `returnToStub` is set, the function writes four instructions `lis r3,0x8000; ori r3,r3,0x1800; mtctr r3; bctr` at `ad[2]` (the `ES_LaunchTitle` call in `__OSLaunchMenu`) instead of the title ID; the stub takes precedence over `returnTo`. | "Wii Menu" in a game jumps into newlauncher's return stub instead of the System Menu (M21). |
| `main.cpp` (before `set_wip_list`) | `returnToStub = hooktype == 0 && nlrs_stub_ok(0x80001800) && nlrs_params_ok(0x80002800)` (signature "STUBHAXX" and parameter block; includes `launch/return_stub/stub_check.h`); the disc ID for WiiRD (`memcpy` to `0x80001800`) is then skipped. | The stub is at `0x80001800`. With the code handler (cheats) that memory is taken, so only without a hook (M21). |
| `apploader.c`, `ChannelHandler.cpp` | `PatchReturnTo` is also called with `returnToStub` (`if(returnTo \|\| returnToStub)`). | Applies to games and channels (M21). |
| all | line endings CRLF → LF | the repository's `.gitattributes` (`eol=lf`) |

### Build options (CMake target `nl_game_booter`)

| Option | Reason |
|---|---|
| `-O1 -Wall -Wextra -DHAVE_CONFIG_H` | as in the upstream Makefile |
| `-Wno-shift-negative-value -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-overread` | as in the upstream Makefile. The last three are marked `FALSE_POSITIVES` there: GCC reports them for fixed memory addresses (`(u32*)0x80000000` and similar). |
| `-std=gnu17` (C only) | GCC 15+ compiles C as C23 by default, where `int (**main)()` means "no arguments" and `apploader.c` no longer matches the pointer type. Upstream was built with gnu17; the code stays unchanged. |
| `-Wa,-mno-regnames` (`patchhook.S` only) | The platform sets `-mregnames` for all assembler files. `patchhook.S` defines `r0`…`r31` itself with `.set`; with `-mregnames` gas reports "invalid register expression". |
| `-Wl,--section-start,.init=0x80A80000` | load address as upstream |
| `-fno-exceptions -fno-rtti` (C++ only) | project rule "no exceptions, no RTTI", applied to all C++ files of the booter, not just the LZ77 bridge (M20) |
| `-ffunction-sections -fdata-sections`, `-Wl,--gc-sections` | unused code is dropped; the booter must stay below `0x80B00000`, even with the error message and console (M20) |

### libogc

Built against the installed libogc (devkitPro, tuxedo based). The only API gap: `__exception_closeall()` no longer
exists. `nl/exception_close.cpp` recreates it as in old libogc:

- `MSR_EE` off
- `MSR_FP|MSR_RI` on
- overwrite each of the 15 exception vectors (`0x100` … `0x1700`) with `rfi`, flush the cache

The game sets its own vectors in `OSInit`. Whether the new `crt0` does something at booter start that the old one
did not can only be checked on hardware (acceptance W1–W9).

## Hand-over format (`include/booter_cfg.h`)

| Constant | Value | Origin |
|---|---|---|
| `NL_WII_CFG_ADDR` | `0x93200000` | plan M9 (instead of the pointer at `0x90100000`, WiiFlow `source/booter/external_booter.cpp`) |
| `NL_FRAG_LIST_ADDR` | `0x93201000` | plan M9 |
| `WBFS_DEVICE_USB` / `WBFS_DEVICE_SDHC` | 1 / 2 | `source/libwbfs/libwbfs.h:12–13`, set in `source/loader/wbfs.c:216`; the booter checks `wbfsDev == 2` (`main.cpp:111`) |
| `kNlVidModeSystem` | 1 | `disc.c:164` `Disc_SelectVMode`, `case 1: // SYSTEM` |
| `kNlConfigByteUnpatched` | `0xCD` | `source/menu/menu_game_boot.cpp:35` (`setLanguage`): do not patch the language |
| `kNlAspectRatioGame` | −1 | `menu_game_boot.cpp:1114`: setting 0 ("game default") minus 1 |
| `kNlDeflickerGame` | 0 | `menu_game_boot.cpp:1121–1122` |
| `kNlVideoWidthGame` | 0 | `menu_game_boot.cpp:1124–1125` |

**`JustDanceGame`**: true if the game title contains "Just Dance" (`menu_game_boot.cpp:1099–1101`,
`wcsstr(hdr->title, L"Just Dance")`).

**`size` of the fragment list**: `source/loader/frag.c:64` (`frag_append`) sets `ff->size = offset + count` on
every append, so the size is the end of the last appended fragment, in 512-byte sectors.

- WBFS: fragments come in disc order, so the end of the last fragment.
- ISO: WiiFlow appends the file's clusters through `_FAT_get_fragments` (its own libfat fork). The length follows
  from the file size, `ceil(file size / 512)`.

The booter checks the size only in `frag_get` (`offset + count > size` → error, `frag.c:120`).

## Licenses

Decision (M11, spec v2 3): newlauncher is licensed under **GPLv3 or later**. The booter therefore contains no file
that is GPLv2 only (without "or later") and no binary blob without source.

- GPLv3 or later: `Config.hpp`, `main.cpp` and the other files without their own header (part of WiiFlow Lite)
- GPLv2 or later: `fst.c/h`, `patchcode.c/h`
- zlib-like ("Altered source versions must be plainly marked"): `ChannelHandler.cpp` (dude, 2010); the file header
  notes the change
- newlauncher's own code (GPLv3 or later): `nl/*`, `src/core/lz77.*`

### Replaced (M11)

| Upstream file | Upstream license | Replacement | How |
|---|---|---|---|
| `lz77.c/h` | GPLv2 only | `src/core/lz77.*` + `nl/lz77_bridge.*` | rewritten from the format description in the M11 plan without opening the old file; host tests with samples from our own Python encoder (`tools/gen_lz77_samples.py`) |
| `video_tinyload.c/h` | GPLv2 only | `nl/progress.c/h` | rewritten from the functional description in the M11 plan without opening the old file; same function names; checked with `tests/wii/progress_probe.c` in Dolphin |

Only the includes in `main.cpp`, `apploader.c`, `disc.c` and `ChannelHandler.cpp` were adjusted.

### Code handler

The WiiFlow headers `codehandler.h` and `codehandleronly.h` were binary data (raw2c) without traceable source. M11
disabled them, M15 deleted and replaced them:

- The source is now `nl/codehandler/codehandler.s` (Dolphin, MIT, see the README there). CMake assembles it and
  generates `codehandler.h`/`codehandler_info.h` in the build directory (`cmake/NlCodehandler.cmake`).
- `fst.c`: `load_handler()` only knows the handler without debugger; the debugger variant had no source.
  Addresses and patch locations come from `codehandler_info.h`; two `_Static_assert`s guard entry point and size.
- CMake sets `NL_BOOTER_CHEATS=1`; `main.cpp` takes `hooktype` from `the_CFG` again (0 without cheats).
- The code list is in MEM1 behind the handler (`codelist`…0x80003000), as upstream. The launcher only stores the
  GCT as a transfer buffer in MEM2 (`NL_GCT_ADDR`); `ocarina_set_codes()` copies it early.
- `tools/tests/test_booter_dol.py` checks that no bytes of the old blobs are in the DOL and that the self-assembled
  handler is contained unchanged.
