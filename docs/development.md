# Development

## Requirements (Windows)

- **devkitPro** with the "Wii Development" component (`C:\devkitPro`), then in its MSYS2 shell:
  `pacman -S --needed wii-dev cmake ppc-freetype ppc-libpng ppc-libvorbisidec ppc-libogg`
- **Host compiler:** `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`
- **Ninja:** `winget install --id Ninja-build.Ninja -e`
- **CMake** 3.25 or newer and **Python 3**
- **Dolphin** in `../wii_tools/Dolphin-x64/` (next to this repository)

Keep the checkout path short on Windows: unpacking Mbed TLS during the Wii build can exceed the path limit.

## Commands

```bash
cmake --preset host && cmake --build --preset host && ctest --preset host   # host unit tests
python -m unittest discover -s tools/tests -v                               # tool tests
python tools/build_wii.py                                                   # Wii build with TLS
python tools/build_wii.py --no-tls                                          # Wii build without TLS
python tools/check_licenses.py build/wii                                    # license entries
```

Start the DOL in Dolphin with a separate development user directory (your own Dolphin setup stays untouched):

```bash
../wii_tools/Dolphin-x64/Dolphin.exe -u ../wii_tools/dolphin-user -e build/wii/newlauncher.dol
```

Deploy to the virtual SD card (launcher plus fixture library from `tests/fixtures/sd/`), or run the smoke test,
which waits for `NL_READY` in the Dolphin log (`--no-fixtures` leaves the card as it is):

```bash
python tools/deploy_dolphin.py
python tools/dolphin_smoke.py
```

CI builds the Wii DOLs with and without TLS, runs the host and tool tests and uploads the DOLs as artifacts.

Rebuild the font subset (only when changing fonts, needs `pip install fonttools`):

```bash
python tools/subset_fonts.py MPLUSRounded1c-Regular.ttf MPLUSRounded1c-Medium.ttf
```

Regenerate the fixture library (dummy headers and empty ROM files only, no games):

```bash
python tools/make_fixtures.py tests/fixtures/sd
```

## Dolphin smoke tests

Self-test (scripted input, no allocation while idle, deliberate crash at the end):

```bash
python tools/dolphin_smoke.py --dol build/wii/nl_selftest.dol --marker "NL_LIBRARY games=12" --marker NL_FAV_SAVED --marker "NL_SEARCH mario=5" --marker "NL_OVERRIDE_SAVED n64/Super Mario 64 (Europe) (En,Fr,De).z64 not64" --marker "NL_PREFLIGHT_FAIL Unsupported GM8P01" --marker "NL_VC_CHANNEL 000100014E4C5443 NL Testkanal" --marker "NL_VCMAP_SAVED n=2" --marker "NL_SELFTEST_COVERS" --marker "NL_ALLOC_IDLE 0" --marker NL_SELFTEST_OK --marker NL_SELFTEST_LOG_SAVED --marker "NL_CRASH id=7" --marker "NL_LANG en" --marker NL_CONFIG_SAVED
```

Sounds and music (UI sounds log `NL_SOUND` with `[debug] log_sounds = 1`; the fixtures put `tone.ogg` and a
broken `header_only.ogg` into the music folder):

```bash
python tools/dolphin_smoke.py --marker "NL_AUDIO ok" --marker NL_READY
python tools/dolphin_smoke.py --marker NL_READY --marker "NL_MUSIC play sd:/newlauncher/music/tone.ogg rate=32000 ch=1" --marker "NL_MUSIC_FAIL sd:/newlauncher/music/header_only.ogg"
python tools/dolphin_smoke.py --autostart "nes/Super Mario Bros. (World).nes" --marker NL_AUDIO_STOP --marker "ARGV 1"
```

Chainload (starts an NES fixture game via `[debug] autostart`; the test DOL `argv_echo` reports its arguments):

```bash
python tools/dolphin_smoke.py --autostart "nes/Zelda (Europe) (Rev 1).nes" --marker "NL_LAUNCH nes/Zelda (Europe) (Rev 1).nes" --marker "NL_ARGV argc=3" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/nes/" --marker "NL_ARGV 2 Zelda (Europe) (Rev 1).nes"
```

N64: the Not64 path (the fixtures point `[n64_not64]` at `argv_echo`) and the VC path through a test channel
whose boot content is also `argv_echo`. `deploy_dolphin.py` and `dolphin_smoke.py` create the channel
"NL Testkanal" (`000100014E4C5443`) in the development NAND:

```bash
python tools/dolphin_smoke.py --autostart "n64/Super Mario 64 (Europe) (En,Fr,De).z64" --marker "NL_N64 n64/Super Mario 64 (Europe) (En,Fr,De).z64 not64" --marker "NL_ARGV argc=2" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV 1 sd:/roms/n64/Super Mario 64 (Europe) (En,Fr,De).z64"
python tools/dolphin_smoke.py --autostart "n64/Mario Kart 64 (Europe).z64" --marker "NL_N64 n64/Mario Kart 64 (Europe).z64 vc" --marker "NL_LAUNCH_VC 000100014E4C5443" --marker "NL_ARGV argc="
```

GameCube (the fixtures register `argv_echo` as Nintendont; it carries version string 6.509 and decodes the
`NIN_CFG` block):

```bash
python tools/dolphin_smoke.py --autostart GM8P01 --marker "NL_NIN ver=6.509 path=/games/Metroid Prime [GM8P01]/game.iso usb=0" --marker "NL_ARGV 0 sd:/apps/argv_echo/boot.dol" --marker "NL_ARGV_NINCFG ver=10 cfg=0x00000088 vid=0x00000000 lang=-1 pads=4 mc=2 id=GM8P path=/games/Metroid Prime [GM8P01]/game.iso"
```

Covers (the fixture PNG is converted to CMPR; `--cover-server` also starts a local HTTP server with two test
covers and triggers the download through `[debug] cover_url` and `fetch_covers`):

```bash
python tools/dolphin_smoke.py --marker "NL_COVER_CONVERTED nes/Super Mario Bros. (World)" --marker "NL_COVERS ready="
python tools/dolphin_smoke.py --cover-server --marker "NL_NET ok" --marker "NL_COVER_DL RMCP01 EN ok" --marker "NL_COVER_DL GM8P01 US ok" --marker "NL_COVER_DL RMGP01 missing" --marker "NL_COVER_DL_DONE ok=2 missing=1 failed=0"
```

Emulator installation (`--emu-server` starts a local OSC-style repository with one app, `fceugx`, whose
`boot.dol` is `argv_echo`):

```bash
python tools/dolphin_smoke.py --emu-server --autostart "nes/Super Mario Bros. (World).nes" --timeout 120 --marker "NL_EMU_LIST ok" --marker "NL_EMU_INSTALL fceugx ok" --marker "ARGV 1"
```

Cloud sync against local fakes: see [cloud-setup.md](cloud-setup.md#testing-without-a-real-account).

## Testing on a Wii

Follow the checklist in [hardware-acceptance.md](hardware-acceptance.md). `SYS_Report` is silent on the Wii, so
all markers go only to `sd:/newlauncher/log.txt` (the last 16 KB, written at `NL_READY`, before every game start
and on exit).

Copy the release DOL to the SD card (drive `E:`) with measurements enabled:

```bash
python tools/wii_deploy.py E: --perf on
```

This copies `boot.dol` and `meta.xml` to `apps/newlauncher/`. Nothing under `newlauncher/` is overwritten;
`--perf on|off` only sets or removes `[debug] perf` in `config.ini`. A card without an `apps/` folder is refused
unless you pass `--init`.

Put the self-test DOL on a separate, empty SD card:

```bash
python tools/wii_deploy.py F: --selftest
```

The self-test plays a fixed input script against the fixture library and writes favorites, history, settings
and VC mappings. The script therefore refuses any card with `wbfs/`, `games/`, `roms/` or `newlauncher/`. At the
end it saves `log.txt` and crashes on purpose to show the crash screen.

Without swapping cards, over Wi-Fi to the open Homebrew Channel:

```bash
python tools/wii_deploy.py --wiiload <wii-ip>
```

Evaluate the measurements (several logs are merged); prints a Markdown table, exit code 1 if a budget is missed:

```bash
python tools/check_perf.py E:\newlauncher\log.txt F:\newlauncher\log.txt
```

With `[debug] perf = 1` the launcher writes:

| Marker | Meaning |
|---|---|
| `NL_PERF boot first_frame_ms=<n> ready_ms=<n> usb_wait_ms=<n> hz=<50\|60>` | once after `NL_READY`: time to first frame and until usable, including USB wait |
| `NL_PERF frames n=<n> slow=<n> max_ms=<x.y> busy=<0\|1>` | every 300 frames: frames over 1.5 frame periods; `busy=1` while scanning, downloading or converting covers |
| `NL_PERF summary ready_ms=… usb_wait_ms=… hz=… frames=… slow=… busy_slow=… max_ms=… cover_n=… cover_p50=… cover_p95=… cover_max=… mem1_free=… mem2_free=… heap_free=…` | before each log write at game start and exit: all budget values of the session; cover latency from first visible request to finished texture (without PNG conversion) |

## Debug settings (tests only)

`[debug] perf = 1` writes the `NL_PERF` lines, `cover_url` replaces the GameTDB address, `fetch_covers = 1`
triggers the cover download after `NL_READY`, `log_sounds = 1` logs every UI sound, `emu_repo` replaces
`[emulators] repo`, `emu_install = <name>,<name>` installs those emulators after `NL_READY` (`autostart` waits
for it).

## Log markers (Dolphin log, OSREPORT)

| Marker | Meaning |
|---|---|
| `NL_READY` | first frame drawn |
| `NL_ATLAS ok` | font atlas built |
| `NL_ERROR <text>` | start aborted |
| `NL_CMD <name>` | command from the HOME menu, gear or settings (`exit_hbc`, `wii_menu`, `open_vc_map`, `fetch_covers`, `power_off`, `cycle_theme`, `settings_changed`, `save_settings`, `rescan`) |
| `NL_ALLOC_IDLE <n>` | self-test: allocations during 120 idle frames |
| `NL_SELFTEST_OK` | self-test passed |
| `NL_SELFTEST_LOG_SAVED` | self-test: `log.txt` written before the deliberate crash |
| `NL_PERF boot\|frames\|summary …` | measurement lines with `[debug] perf = 1` |
| `NL_PANIC <expression> <file>:<line>` | violated `NL_ASSERT` |
| `NL_CRASH id=<n> pc=… lr=…` | CPU exception, crash screen active |
| `NL_CONFIG ok\|default` | `config.ini` read, or created with defaults |
| `NL_MOUNT <lib>` | library device (`sd:` or `usb:`) |
| `NL_LIBRARY games=<n> source=<index\|scan>` | library ready |
| `NL_SCAN_DONE games=<n> added=<a> removed=<r>` | scan finished (first or background scan) |
| `NL_INDEX_SAVED` | `index.bin` written |
| `NL_FAV_SAVED <key>` | favorites saved |
| `NL_SEARCH mario=<n>` | self-test: hits for the search "MARIO" |
| `NL_EMULATORS ok\|default` | `emulators.ini` read, or defaults used |
| `NL_PREFLIGHT_FAIL <kind> <key>` | start refused (`Unsupported`, `EmuMissing`, `EmuInvalid`, `RomMissing`, `ArgsTooLong`, `NoMemory`, `NoN64Path`), dialog open |
| `NL_NAND channels=<n>` | at start: number of installed channels |
| `NL_VC_CHANNELS n=<n>`, `NL_VC_CHANNEL <titleid> <name>` | mapping dialog opened: channel list |
| `NL_OVERRIDE_SAVED <key> <auto\|vc\|not64\|wii64>` | start method saved to `games.ini` |
| `NL_VCMAP_SAVED n=<count>` | `n64_vc.ini` saved |
| `NL_N64 <key> <vc\|not64\|wii64>` | chosen N64 method at start |
| `NL_LAUNCH_VC <titleid>` | channel start begins |
| `NL_HISTORY_SAVED` | history saved before start |
| `NL_AHBPROT on\|off` | hardware access at start; `on` means locked (e.g. started via wiiload), no GameCube start |
| `NL_NIN ver=<x.y> path=<path> usb=<0\|1>` | GameCube start through Nintendont |
| `NL_ARGV_NINCFG …` | test DOL `argv_echo`: decoded `NIN_CFG` block from `argv[1]` |
| `NL_LAUNCH <key>` | chainload begins |
| `NL_AUTOSTART <key>` | test mode: `[debug] autostart` starts the game after `NL_READY` |
| `NL_ARGV argc=<n>`, `NL_ARGV <i> <value>` | test DOL `argv_echo`: received arguments |
| `NL_COVER_CONVERTED <stem>` | PNG converted to CMPR and cached |
| `NL_COVER_FAIL <stem> <reason>` | cover unreadable (placeholder stays) |
| `NL_COVERS ready=<n>` | cover thread idle: number of loaded covers |
| `NL_SELFTEST_COVERS ready=<n>` | self-test: covers loaded before the idle window |
| `NL_NET ok`, `NL_NET_FAIL <code>` | network ready or unavailable |
| `NL_COVER_DL_START jobs=<n>` | number of missing Wii/GameCube covers |
| `NL_COVER_DL <id6> <region> ok`, `NL_COVER_DL <id6> missing\|failed` | result per game |
| `NL_COVER_DL_ABORT failures=3` | three failures in a row: the remaining games are counted as `failed` |
| `NL_COVER_DL_DONE ok=<n> missing=<n> failed=<n>` | download finished, dialog open |
| `NL_EMU_LIST ok n=<n>`, `NL_EMU_LIST fail` | OSC list loaded or not |
| `NL_EMU_INSTALL <name> ok files=<n>` | emulator installed or updated |
| `NL_EMU_INSTALL <name> fail <nospace\|download\|badsize\|badzip\|write\|cancelled> <detail>` | installation failed; `tmp/` cleaned, old version stays |
| `NL_EMU_DEBUG_INSTALL n=<n>` | test mode: number of apps from `[debug] emu_install` found in the list |
| `NL_EMU_DONE not64=<0\|1> wii64=<0\|1>` | installations finished; N64 DOLs rechecked |
| `NL_LANG <de\|en>` | active language |
| `NL_CONFIG_SAVED` | settings saved to `config.ini` |
| `NL_RESCAN start` | "Rescan library" started |
| `NL_BATTERY p1=<n> p2=<n> p3=<n> p4=<n>` | HOME menu opened: battery per Wii Remote (0–4, −1 = not connected) |
| `NL_AUDIO ok`, `NL_AUDIO_FAIL <reason>` | sound ready or unavailable |
| `NL_AUDIO_STOP` | sound stopped before game start or exit |
| `NL_SOUND <move\|confirm\|back\|open\|error>` | UI sound played (only with `[debug] log_sounds = 1`) |
| `NL_MUSIC play <path> rate=<hz> ch=<n>` | new track |
| `NL_MUSIC_FAIL <path>`, `NL_MUSIC_OFF` | track not playable, or music turned off |
| `NL_WARN <text>` | non-critical error (e.g. broken cache discarded) |
