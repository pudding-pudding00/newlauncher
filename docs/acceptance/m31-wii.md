# M31 Hardware Acceptance Checklist (`[WII-HW]`)

Execute during the consolidated **Stage 6** real-Wii session (after Stages 1–5 are merged and green).

## Preconditions
- Real Wii / vWii connected to LAN; `[network] wiiload = confirm` enabled in Settings.
- `newlauncher` launched from SD (`sd:/apps/newlauncher/boot.dol`) so `return_stub` is active.

## Checks
- [ ] **`.dol` push:** Send a test `.dol` over LAN to `<wii-ip>:4299` via `wiiload` or `tools/wii_deploy.py --wiiload <wii-ip>`. Verify `WiiloadScene` displays sender IP, filename, `DOL`, size, and args.
- [ ] **Decline (`B`):** Press `B` to decline; verify launcher stays responsive and logs `NL_WIILOAD decline`.
- [ ] **Accept (`A`) & `return_stub`:** Resend `.dol`, accept with `A`; verify app runs and `exit()` returns cleanly to `newlauncher`.
- [ ] **`.elf` push:** Send a compiled PowerPC `.elf`; verify in-memory ELF → DOL conversion runs cleanly.
- [ ] **`.zip` bundle push:** Send an `apps/<name>/boot.dol` `.zip`; verify atomic extraction to `sd:/apps/<name>/` and execution.
- [ ] **Parental PIN gate:** Enable Parental Controls PIN, set `wiiload = auto`; verify incoming payload still prompts instead of auto-running.
