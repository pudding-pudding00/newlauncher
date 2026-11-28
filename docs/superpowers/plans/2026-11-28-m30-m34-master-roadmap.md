# M30–M34 Master Roadmap (No-Wii-First Execution Guide)

Tracks: [Issue #5](https://github.com/pudding-pudding00/newlauncher/issues/5) · Created: 2026-11-28

> **Core Execution Rule:** Prioritize and complete **all work that does not require a real Wii** (`[HOST]`, `[BUILD]`, `[DOLPHIN]`, and CI/CodeQL verification) **before** touching real Wii hardware. Real Wii hardware (`[WII-HW]`) is used only in the consolidated final acceptance phase once all software, builds, host tests, and Dolphin smokes are green.

## Environment Tags Used Across All Plans

| Tag | Environment | Verification Command | Wii Required? |
|---|---|---|---|
| **`[HOST]`** | PC host compiler / Python 3 | `cmake --preset host && cmake --build --preset host && ctest --preset host` and `python -m unittest discover -s tools/tests` | **No** |
| **`[BUILD]`** | PC devkitPro Wii cross-compile | `python tools/build_wii.py`, `python tools/build_wii.py --no-tls`, `python tools/check_licenses.py build/wii` (plus `--dolphin-dev-tls` after M33) | **No** |
| **`[DOLPHIN]`** | Dolphin emulator (dev user dir) | `python tools/dolphin_smoke.py ...` (`-u ../wii_tools/dolphin-user`) | **No** |
| **`[WII-HW]`** | Real Wii / vWii console | Acceptance checklists under `docs/acceptance/m30-wii.md`–`m33-wii.md` | **Yes (Final Phase Only)** |

## Global Rules (From `AGENTS.md`)

- Always work on a dedicated feature branch per milestone (e.g., `fix/m34-code-scanning`, `feat/m31-wiiload`, `feat/m32-ftp`, `feat/m30-channel-autostart`, `feat/m33-libwiitls-1.1`), never directly on `main`.
- **TDD:** Write a failing test first, then implement until green.
- All GitHub-facing artifacts (commits, PRs, docs, issues, new code comments) must be in **English**. In-app UI strings must support both **German and English** (`src/ui/strings.hpp`).
- `src/core/` must never include `<ogc/...>`; no exceptions (`throw`), no RTTI; 0 warnings on Wii builds with and without TLS.
- Never commit ROMs, ISOs, WADs, third-party banners, Wii Common Key, tokens, LAN IPs, MAC addresses, or local machine paths.
- Before ANY `[WII-HW]` step that writes to a Wii SD card or real Wii NAND, **ask the user explicitly** and remind them to have a current **BootMii NAND backup**.

---

## Recommended Execution Order (All Non-Wii Work First)

Execute Stages 1–5 completely on PC + Dolphin (each as its own PR merged once CI is green). Do **not** deploy to a real Wii during Stages 1–5.

| Stage | Milestone | Branch | Spec & Plan | Blocking Dependencies | Wii Needed? |
|---|---|---|---|---|---|
| **Stage 1** | **M34 — Code Scanning Remediation** (all 17 open CodeQL alerts) | `fix/m34-code-scanning` | [Spec](../specs/2026-11-28-m34-code-scanning-remediation-design.md) · [Plan](2026-11-28-m34-code-scanning-remediation.md) | None — start immediately | **No** |
| **Stage 2** | **M31 — Remote App Loader (`wiiload` v0.5: `.dol`, `.elf`, `.zip`)** | `feat/m31-wiiload` | [Spec](../specs/2026-11-28-m31-wiiload-remote-exec-design.md) · [Plan](2026-11-28-m31-wiiload-remote-exec.md) | None | **No** (all tasks `[HOST]`/`[BUILD]`/`[DOLPHIN]` done here) |
| **Stage 3** | **M32 — Network File Transfer (Passive FTP, port `21`)** | `feat/m32-ftp-transfer` | [Spec](../specs/2026-11-28-m32-ftp-transfer-design.md) · [Plan](2026-11-28-m32-ftp-transfer.md) | None | **No** (all tasks `[HOST]`/`[BUILD]`/`[DOLPHIN]` done here) |
| **Stage 4** | **M30 — System Menu Channel (`UNLA`) & Priiloader Autostart** | `feat/m30-channel-autostart` | [Spec](../specs/2026-11-28-m30-channel-autostart-design.md) · [Plan](2026-11-28-m30-channel-autostart.md) | None | **No** (all PC/Dolphin tasks done here; NAND/WAD install deferred to Stage 6) |
| **Stage 5** | **M33 — `libwiitls v1.1.1` Upgrade & Beneficial Features** | `feat/m33-libwiitls-1.1` | [Spec](../specs/2026-11-28-m33-libwiitls-1.1-design.md) · [Plan](2026-11-28-m33-libwiitls-1.1.md) | Upstream tag `v1.1.1` in `pudding-pudding00/libwiitls` *(may be moved ahead of Stage 2–4 once tagged)* | **No** (all `[HOST]`/`[BUILD]`/`[DOLPHIN]` done here; real Wii TLS bench deferred to Stage 6) |
| **Stage 6** | **Consolidated `[WII-HW]` Hardware Acceptance Pass** | `docs/m30-m33-hw-acceptance` | `docs/acceptance/m30-wii.md`–`m33-wii.md` | Stages 1–5 merged & green; **explicit user permission** for SD + NAND writes | **Yes (Single Session)** |

---

## Stage 6: Consolidated Real-Wii Hardware Verification Runbook (`[WII-HW]`)

Execute Stage 6 **only after Stages 1–5 are complete and green in CI + Dolphin**, batching all real-console checks into one organized hardware session to minimize console friction and SD swaps:

1. **Pre-Flight Safety Gate (Mandatory):**
   - Ask the user for explicit confirmation before writing to the Wii SD card.
   - Ask the user for explicit confirmation before any real-NAND write (Priiloader / `UNLA` WAD) and remind them to verify a current **BootMii NAND backup** exists.
   - Confirm console type (**Wii** vs. **vWii**) before touching any channel WAD.
2. **Deploy once (via SD or `wiiload`):**
   - Build production release DOL (`python tools/build_wii.py` — **never** `--dolphin-dev-tls`) + `forwarder.dol`.
   - Deploy `boot.dol`, `forwarder.dol`, `meta.xml`, `icon.png`.
3. **Run Acceptance Checklists in Low-Risk → High-Risk Order:**
   - **First (Read-only / RAM / SD only — zero NAND writes):**
     - `docs/acceptance/m33-wii.md`: Verify production `libwiitls 1.1.1` real IOS entropy init, HTTP/2 cover speedup (`[debug] perf=1`), DNS cancel, SNTP offset cache on second boot, interrupted download resume, and cloud sync.
     - `docs/acceptance/m31-wii.md`: Push `.dol`, `.elf`, and `.zip` over LAN to port `4299` (`wiiload` + `tools/wii_deploy.py --wiiload <wii-ip>`), confirm dialog & Parental PIN gate, verify `exit()` returns to newlauncher via `return_stub`.
     - `docs/acceptance/m32-wii.md`: Open Transfer scene, connect via FileZilla/WinSCP on port `21`, test SD FAT32 + USB FAT32 (large file transfer, RAM stability) + USB NTFS read-only rejection + post-transfer library rescan.
   - **Last (NAND / Boot Changes — only after user confirmation & BootMii backup):**
     - `docs/acceptance/m30-wii.md`: Install `forwarder.dol` in Priiloader (`Autoboot: Installed File`), verify cold-boot autostart, verify `HomeMenu` → real Wii System Menu via `"Pune"` magic (no reboot loop), test locally packed `UNLA` channel WAD (matching Wii or vWii target), verify `UNLA` is hidden from launcher library and self-update persists without reinstalling the channel.
4. **Record Results:** Update `docs/acceptance/m30-wii.md`–`m33-wii.md` and `docs/STATUS.md` in a clean PR.
