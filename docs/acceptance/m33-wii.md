# M33 Hardware Acceptance Checklist (`[WII-HW]`)

Execute during the consolidated **Stage 6** real-Wii session (after Stages 1–5 are merged and green).

## Preconditions
- Production `build/wii/newlauncher.dol` built with `python tools/build_wii.py` (**never** `--dolphin-dev-tls`).
- Verify `CMakeCache.txt` has `WTLS_DOLPHIN_DEV_ENTROPY:BOOL=OFF` and `WTLS_KEYLOG:BOOL=OFF`.

## Checks
- [ ] **Production Entropy Init:** Boot `newlauncher.dol` on real Wii; verify `libwiitls 1.1.1` initializes cleanly with real IOS jitter entropy.
- [ ] **HTTPS / HTTP/2 Cover Download:** Download covers over HTTPS; verify 0 TLS errors and HTTP/2 speedup (`[debug] perf=1`).
- [ ] **Cloud Sync:** Test Google Drive / OneDrive / Dropbox sync over HTTPS with `libwiitls 1.1.1`.
- [ ] **Self-Update:** Verify `update.ini` (including `sha256`) downloads and installs cleanly.
