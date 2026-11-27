# Project Status

Last updated: 2026-11-26. All planned milestones through **M29** (Cloud Saves with Google Drive, OneDrive, and Dropbox + pairing page, QR encoder, extended keyboard, cover ordering/batching, and taskbar pointer UX) are implemented and merged into `main`. Hardware acceptance for M21–M29 is still open. Agent rules live in [AGENTS.md](../AGENTS.md).

## Completed Milestones

| Milestone | Summary | PR |
|---|---|---|
| M0–M10 | v1: Launcher, library, game booter, emulators, covers, performance | through #13; v2 spec #14 |
| M11 | Licenses and TLS (`wii_tls` vendored, HTTPS default sources) | #15 |
| M12–M17 | Universal library, system detection, per-game options, cheats, filesystems, UX polish | through #23 |
| M18 | Banners: static GC banners, animated channel banners with audio | #24, #25 |
| – | `wii_tls` updated to **v0.1.0**, handoff docs | #26, #27 |
| M19 | Optical discs (Wii & GC via Nintendont), EmuNAND & channels, vWii polish | #28 |
| – | Emulator "Install all", live Wi-Fi status in bottom bar | #30, #31 |
| M20 | Direct ROM launch, return stub, TCP remote control (port 4300), self-update, `wii_tls` v0.2.0 | #33 |
| M21 | "Wii Menu" returns to newlauncher via return stub; auto cover fetch after scan | #34 |
| M22 | Retro covers (NES–GBA) from `libretro-thumbnails` (HTTPS) + `missing.txt` | #36 |
| M23 | "Clear cover cache", pointer edge-scrolling, console-matched tile aspect ratios, perf stats | #37 |
| – | `wii_tls` updated to **v0.3.0** and **v1.0.0** | #38, #39 |
| M24 | Covers ordered by console generation, title/year sort, mixed grids, SD/device cover storage | #40 |
| M25 | Robust Wi-Fi reconnect/backoff, gateway check, status icon & info window | #43 |
| M26 | Local saves (backup, restore with rollback journal, real NAND via ISFS permission patch) | #45 |
| M27 | Cloud saves core + Google Drive (device-code OAuth, pre-launch sync gate, conflict dialog, outbox) | #46 |
| M28 | OneDrive (Graph AppFolder), provider switching, "Sync all", sign-out with token revocation | #47 |
| M29 | Dropbox (PKCE), LAN pairing page (HTTP port 4301), QR encoder, layered 128-char keyboard | #48, #49 |
| – | Cover top-to-bottom FIFO loading, 6x `.tex` batching, Favorites/Recent priority; taskbar pointer & placeholder fixes | #50, #51, #52, #53 |
| – | `wii_tls` switched to the public **libwiitls v1.0.0** (`pudding-pudding00/libwiitls`, Mbed TLS 4.1.2 LTS, HTTP/2) | this PR |

CI (`.github/workflows/build.yml`) builds Wii DOLs with and without TLS and runs all host and Python tool tests.

## Open Items

### 1. Hardware Acceptance on Real Wii
- **M20:** R1–R6, F1, U2 **passed** ([acceptance/m20-wii.md](acceptance/m20-wii.md)). **U1** (GitHub Release self-update) awaits the first published release.
- **M11–M19, M21–M29, and v1/M7 ([hardware-acceptance.md](hardware-acceptance.md)):** Formal checklists remain open (unchecked).
- **M26 Spike S1 (real hardware, read-only check done):** Opening the Save Manager scene on a Wii game (`RUUE01`) under stock IOS 58 with AHBPROT off succeeded (`nand:/title/00010000/.../data` opened without "NAND locked"). Actual NAND write/restore remains untested on hardware (ask user and recommend BootMii NAND backup first).
- **M27–M29 Cloud:** Requires user-provided OAuth app credentials in `sd:/newlauncher/config.ini` (see [cloud-setup.md](cloud-setup.md)).

### 2. Known Minor Technical Notes
- **Flaky Dolphin smoke `--remote-check`:** 2 of 11 runs reported `REMOTE KEY (Fokus blieb …)`, likely due to a Dolphin simulated IR pointer event resetting tile focus.
- **Upstream libwiitls (`z_errmsg`):** `z_errmsg` still collides with zlib 1.3.1 despite `Z_PREFIX` in libwiitls 1.0.0; worked around with `-Dz_errmsg=wtls_z_errmsg` in CMake.
- **Side finding (M13):** cIOS slots 248–251 appear as `other` instead of `d2x` in SysInfo because `wii_disc.cpp` cannot read `.app` under stock IOS 58 without ISFS patches; slot 249 is verified d2x.
- **Deferred / Out-of-scope enhancements:** Parallel cover downloads (would require `wii_tls` changes), `txt1`/`wnd1` banner elements, `.wbf1`/WBFS banner extraction.

## Local Development Notes

- Dolphin dev user directory: `../wii_tools/dolphin-user` (never touch the user's private Dolphin config).
- On Windows, host tests require WinLibs GCC + Ninja and **Kitware CMake** (MSYS2 CMake conflicts with WinLibs Ninja).
- Never open raw TCP connections to port 4299 (it blocks `wiiload`). Use `python tools/wii_remote.py --host <wii-ip> ...` or `python tools/wii_deploy.py --wiiload <wii-ip>`.
- Always ask the user before writing to the Wii SD card or real NAND.
