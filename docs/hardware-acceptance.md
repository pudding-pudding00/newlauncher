# Hardware acceptance (M7)

Acceptance of newlauncher on a real Wii against the **release DOL** `build/wii/newlauncher.dol`. Dolphin covers
graphics, input and the start paths well. Timing, USB spin-up, cIOS, real emulators and channels only show on
hardware (spec §10).

Each item has a step and an expected result. Enter PASS or FAIL under "Result"; for FAIL add a short note and a
row under [Findings](#findings).

## Preparation

1. Build: `python tools/build_wii.py`
2. Release card (your normal SD card with the Homebrew Channel):
   ```bash
   python tools/wii_deploy.py E: --perf on
   ```
   Copies `apps/newlauncher/boot.dol` and `meta.xml`. Existing data under `newlauncher/` (`config.ini`,
   `emulators.ini`, favorites, history, `cache/`) stays unchanged. The only exception is `--perf on`: it sets
   `perf = 1` under `[debug]` in `config.ini`.
3. Self-test card (a **second, empty** SD card, only for X1 and P3):
   ```bash
   python tools/wii_deploy.py F: --selftest
   ```
4. Alternatively without swapping cards, over Wi-Fi (HBC open, IP shown in its bottom right corner):
   ```bash
   python tools/wii_deploy.py --wiiload <wii-ip>
   ```
   For P1/P2, still set `--perf on` on the card once: wiiload transfers only the DOL.
5. Afterwards: `python tools/wii_deploy.py E: --perf off`

## V – Prerequisites

Only note them down.

| ID | Step | Expected | Result |
|---|---|---|---|
| V1 | Wii model (RVL-001/-101) and System Menu version (Wii options, top right) | noted | |
| V2 | HBC version (in the HBC, HOME button) | noted | |
| V3 | cIOS 249: d2x version and base IOS according to the d2x installer or a USB loader's start screen | noted | |
| V4 | Nintendont version (title screen) | noted | |
| V5 | USB drive: model, file system, cluster size, sector size. Windows: `fsutil fsinfo ntfsinfo E:` or `fsutil fsinfo sectorinfo E:`; for FAT32 see the drive properties | noted | |
| V6 | SD card: size, FAT32 | noted | |
| V7 | Video setting in the Wii menu: 50/60 Hz, 480p, 4:3/16:9 | noted | |

## S – Start and drives

| ID | Step | Expected | Result |
|---|---|---|---|
| S1 | Start newlauncher in the HBC | splash appears at once, no black screen longer than ~0.5 s | |
| S2 | Drive connected and awake, `library = usb` | library appears without a dialog | |
| S3 | Plug in the drive fresh, or cold start | "waiting for USB" with countdown, then the library | |
| S4 | Unplug the drive, start | dialog after `usb_timeout` seconds; "use SD" opens the SD library | |
| S5 | HOME → Settings → Library device: SD card, restart | library from SD, restart notice appeared before | |
| S6 | Delete `newlauncher/cache/index.bin` on the library device, start | scan progress, then usable | |
| S7 | With `--perf on`: let the drive sleep or plug it in cold, `usb_timeout` shorter than spin-up, press "retry" once in the dialog, time the wait; then exit cleanly and run `check_perf.py` | `usb_wait_ms` in `NL_PERF summary` ≥ measured time (including dialog); start budget PASS | |

## D – Display

| ID | Step | Expected | Result |
|---|---|---|---|
| D1 | Wii menu set to 4:3 / 480i | layout complete, nothing cut off, circles round | |
| D2 | 16:9 / 480p (component cable) | wide layout, circles round, no flicker | |
| D3 | PAL50 (if selectable, EURGB60 off) | stable picture, animations at the same speed | |
| D4 | Theme light, dark, automatic | switches with a fade, automatic follows the time | |
| D5 | Look at titles with umlauts | sharp text, umlauts correct | |

## E – Input

| ID | Step | Expected | Result |
|---|---|---|---|
| E1 | Point the Wii Remote at the screen | pointer follows without jitter, tiles react | |
| E2 | Wii Remote D-pad | focus moves, repeats while held | |
| E3 | Classic Controller | navigation and A/B like the Wii Remote | |
| E4 | GameCube controller | navigation and A/B like the Wii Remote | |
| E5 | Connect four Wii Remotes, open the HOME menu | battery level P1–P4 shown | |
| E6 | Toggle the debug overlay: Wii Remote 1 + 2, GameCube Z + Start | overlay with frame statistics appears and disappears | |

## B – Library

| ID | Step | Expected | Result |
|---|---|---|---|
| B1 | Favorite a game, start a game, come back | favorite and "recently played" survive a restart | |
| B2 | A–Z jump bar | jumps to the letter | |
| B3 | Search with the on-screen keyboard | results while typing | |
| B4 | Open the detail view | cover, metadata, start method | |
| B5 | HOME → Settings → "Rescan library" | toast, new games appear, UI stays usable | |

## L – Emulators

Risk from spec §10: the emulators' `argv` scheme. One ROM of your own each must start **directly in the
emulator**, without its file browser.

| ID | Step | Expected | Result |
|---|---|---|---|
| L1 | Start an NES ROM (FCE Ultra GX) | game runs | |
| L2 | Start an SNES ROM (Snes9x GX) | game runs | |
| L3 | Start a GB ROM | game runs | |
| L4 | Start a GBC ROM | game runs | |
| L5 | Start a GBA ROM (Visual Boy Advance GX) | game runs | |
| L6 | Start an N64 ROM with Not64 | game runs | |
| L7 | Start an N64 ROM with Wii64 | game runs | |
| L8 | Rename the emulator DOL, start the ROM | preflight dialog names the missing emulator, launcher stays usable | |
| E1 | Install/update emulators on the real Wii (Wi-Fi): Settings → Emulators → "Check for updates", install a missing one, then "Update all"; press B once during a download | list loads (`NL_EMU_LIST ok`), install logs `NL_EMU_INSTALL … ok`, row shows up to date; cancel is reported, old version still starts, `sd:/newlauncher/tmp/` is empty | |

## N – N64 Virtual Console

| ID | Step | Expected | Result |
|---|---|---|---|
| N1 | HOME → Settings → "Map VC injects" | channel list with banner names (risk, spec §10) | |
| N2 | Save a mapping, restart | mapping stays (`n64_vc.ini`) | |
| N3 | Start the inject | channel starts the game | |

## G – GameCube

Requirement: Nintendont 6.493 or newer at `sd:/apps/nintendont/boot.dol`, launcher started from its HBC entry.

| ID | Step | Expected | Result |
|---|---|---|---|
| G1 | Start a GameCube game from SD (`sd:/games/<Title> [ID6]/game.iso`) | Nintendont starts the game without its own menu; log shows `NL_NIN ver=… usb=0` | |
| G2 | Start a GameCube game from USB (library device USB) | game starts; `NL_NIN … usb=1` | |
| G3 | Save to the memory card in game, restart the Wii, start again and load | save is there (emulated card under `saves/`) | |
| G4 | Play a two-disc game with `disc2.iso` up to the disc change | Nintendont switches to disc 2, or note "no two-disc game available" | |
| G5 | Rename `sd:/apps/nintendont/boot.dol`, start a GameCube game | dialog reports Nintendont missing with the path, launcher stays usable | |
| G6 | Start the launcher via `wiiload`, start a GameCube game | log `NL_AHBPROT on`, dialog about missing hardware access | |
| G7 | Use a GameCube controller and Wii Remote + Classic Controller in game | both control the game | |

## C – Covers

| ID | Step | Expected | Result |
|---|---|---|---|
| C1 | HOME → Settings → "Download missing covers" (Wi-Fi) | progress, then result dialog; UI stays usable | |
| C2 | Put a PNG into `covers_src/`, show the game | cover appears after conversion, `.tex` is then in the cache | |
| C3 | Scroll quickly through the whole library | no stutter, covers fade in | |

## A – Audio and settings

| ID | Step | Expected | Result |
|---|---|---|---|
| A1 | `.ogg` in `sd:/newlauncher/music/`, music on | music plays after start | |
| A2 | Change the volume | audible at once | |
| A3 | UI sounds off and on again | clicks stop and return | |
| A4 | Language German/English | texts change at once | |
| A5 | Change settings, restart | values stay | |
| A6 | Start a game while music plays | music ends before the game starts, no click in game | |

## H – HOME menu

| ID | Step | Expected | Result |
|---|---|---|---|
| H1 | HOME → back to the HBC | HBC appears | |
| H2 | HOME → Wii menu | Wii menu appears | |
| H3 | HOME → power off | Wii turns off (standby or off, depending on the WiiConnect24 setting) | |

## X – Crash

| ID | Step | Expected | Result |
|---|---|---|---|
| X1 | Self-test card: start the self-test in the HBC and wait without input | script runs visibly, then crash screen with exception 7 (photo); RESET returns to the HBC | |

## P – Performance

Budgets from spec §7. Measurement runs only with `[debug] perf = 1`.

| ID | Step | Expected | Result |
|---|---|---|---|
| P1 | Release card with `--perf on`, cache present (second start), drive awake: start, scroll for 2 minutes, "Download missing covers", "Rescan library", start a game | game starts | |
| P2 | `python tools/check_perf.py E:\newlauncher\log.txt F:\newlauncher\log.txt` | exit 0, table pasted below | |
| P3 | Self-test card after X1: `F:\newlauncher\log.txt` | contains `NL_ALLOC_IDLE 0` and `NL_SELFTEST_OK` | |

`log.txt` is a ring buffer with the last 16 KB. Long sessions lose `NL_PERF boot` and early windows.
`check_perf.py` then judges from the last `NL_PERF summary` (which also contains `busy_slow`, the slow frames
during background work), marks missing lines as N/A and points out missing window lines. The measurement's own
log writes (`NL_READY`, `NL_COVERS`, `NL_COVER_DL_DONE`) do not count as slow frames.

Result of `check_perf.py`:

<!-- paste table here -->

## Findings

| ID | Observation | Cause | Fix commit | Rechecked |
|---|---|---|---|---|
| | | | | |
