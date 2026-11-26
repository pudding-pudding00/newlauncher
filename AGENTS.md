# Agent Guidelines (All Harnesses)

This file applies to every AI agent harness (Claude Code, Codex, Cursor, Antigravity, etc.). For current project status and open tasks, see [docs/STATUS.md](docs/STATUS.md). Read it first.

## Language & Public Repository Rules

- **GitHub & Repository Language:** All GitHub-facing artifacts — commit messages, PR titles and descriptions, documentation, issues, and new code comments — must be in **English**.
- **Public Repository Hygiene:** This is a **public** repository. Never commit personal data, local LAN IPs, MAC addresses, local machine/user paths, OAuth client IDs/secrets, tokens, or private configuration files. Use generic placeholders (e.g. `<wii-ip>`, `<repo-root>`) in docs.
- **In-App UI:** User-facing UI strings in the launcher itself must still support both German **and** English (`src/ui/strings.hpp`).

## Workflow

- **Spec → Plan → Implementation.** Specs live under `docs/superpowers/specs/`, plans under `docs/superpowers/plans/`. The spec is binding.
- Every milestone delivers three things: host unit tests for everything in `core/`, Dolphin smokes (where Dolphin exercises the behavior), and a hardware acceptance checklist under `docs/acceptance/mNN-wii.md`.
- **TDD:** Write a failing test first, then implement the code.
- Work autonomously and continue after completed steps without waiting for confirmation unless a destructive/hardware-write rule applies.
- Merge PRs once CI is green and review is complete: `gh pr merge N --merge`. No auto-merge, no force-push. Always work on a feature branch, never directly on `main`.
- Do not poll CI in a loop. Wait once, blockingly, with `gh run watch <id> --exit-status`.

## Hard Rules

- **Game files:**
  - Never commit ROMs, ISOs, or WADs.
  - Do not commit third-party banner files.
  - No Common Key in source code.
- **Dolphin:**
  - Only invoke with `-u` and the dev user directory `../wii_tools/dolphin-user`.
  - Never touch the user's private Dolphin configuration.
  - No broad process kills.
- **Real Wii Hardware:**
  - Always ask the user before copying/writing anything to the Wii SD card.
  - Always ask the user before the first write to real Wii NAND (and recommend a BootMii NAND backup).
  - No softmodding without known System Menu version and MAC address.
- **Privacy:**
  - Never log user email addresses, OAuth tokens/codes, or parental control PINs.
  - The launcher never writes to SYSCONF.
- **wii_tls** (`third_party/wii_tls`):
  - `wtls_*` calls run only on the `NetService` thread.
  - Never fall back from HTTPS to HTTP.
  - Never add a switch that disables certificate verification.
  - Never commit private keys.
  - Update only via `python tools/vendor_wii_tls.py <checkout> --rev <tag>`.
- **Code:**
  - `src/core/` must not include `<ogc/...>` so it stays host-testable.
  - No `throw` (exceptions disabled), no RTTI.
  - Wii builds must compile with **0 warnings**, both with TLS (`python tools/build_wii.py`) and without TLS (`--no-tls`).
- **Third-party material:**
  - External references/spikes stay outside the repository.
  - Downloaded files get their own folder. Python scripts on them run with `python -I`.

## Commits & PRs

- Conventional commit style in English: `feat: …`, `fix: …`, `docs: …`, `chore: …`.
- Each harness appends its own `Co-Authored-By` trailer if applicable.

## Key Commands

```bash
cmake --preset host && cmake --build --preset host && ctest --preset host   # Host tests (core/ & ui/)
python -m unittest discover -s tools/tests                                  # Python tool tests
python tools/build_wii.py            # Wii build with TLS    -> build/wii/newlauncher.dol
python tools/build_wii.py --no-tls   # Wii build without TLS -> build/wii-notls/
python tools/check_licenses.py build/wii
python tools/dolphin_smoke.py --marker NL_… [--debug key=value …]           # Smoke test in Dolphin
python tools/wii_deploy.py E: [--perf on|off] [--remote on|off]             # Deploy to SD card (ask first!)
python tools/wii_deploy.py --wiiload <wii-ip>                               # Load DOL into Wii RAM via wiiload
```
