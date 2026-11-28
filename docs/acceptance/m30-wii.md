# M30 Hardware Acceptance Checklist (`[WII-HW]`)

Execute **last** during the consolidated **Stage 6** real-Wii session — **only after explicit user confirmation and a verified current BootMii NAND backup**.

## Pre-Flight Safety Gate
- [ ] Confirm console type: **Wii** vs. **vWii** (never install a Wii WAD on vWii or vice versa).
- [ ] Confirm current **BootMii NAND backup** exists.

## Checks
- [ ] **Priiloader Autostart:** Copy `forwarder.dol` to `sd:/apps/newlauncher/forwarder.dol`, install in Priiloader (`Load/Install File` → `Autoboot: Installed File`). Cold-boot Wii and verify `newlauncher` starts automatically (`NL_FWD boot=sd`).
- [ ] **Loop-Free System Menu Exit:** Open HOME menu → Wii Menu. Verify `"Pune"` magic boots the real Wii System Menu without looping back into `newlauncher`.
- [ ] **`UNLA` Channel WAD:** Stage with `python tools/build_channel.py --target <wii|vwii> --dol build/wii/forwarder.dol --out build/channel`, pack locally, and install via YAWM. Launch `UNLA` from System Menu and verify `newlauncher` boots.
- [ ] **Self-Channel Hidden:** Verify `UNLA` (`00010001-554E4C41`) does **not** appear in `newlauncher`'s Channels list.
- [ ] **Self-Update Persistence:** Update `sd:/apps/newlauncher/boot.dol` and verify `UNLA` boots the new version without reinstalling the channel.
