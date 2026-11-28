# M32 Hardware Acceptance Checklist (`[WII-HW]`)

Execute during the consolidated **Stage 6** real-Wii session (after Stages 1–5 are merged and green).

## Preconditions
- Real Wii / vWii connected to LAN; SD card (FAT32) and USB drive (FAT32 and/or NTFS) attached.
- FTP client on PC (e.g. FileZilla or WinSCP) configured for **Passive Mode (PASV)** and **max 1 simultaneous connection**.

## Checks
- [ ] **Open Transfer Scene:** Open Settings → File Transfer (FTP); verify IP, port (`21`), ephemeral 6-digit PIN, and mounted drives (`RW`/`RO` badges) are displayed.
- [ ] **Auth & Lockout:** Verify wrong password returns `530`; valid user/PIN logs in (`230`).
- [ ] **Virtual FS Jail:** Verify `/` lists `sd` / `usb` only — never `nand`; `CWD /nand` returns `550`.
- [ ] **SD & USB FAT32 Transfer:** Upload and download a large file (> 50 MiB); verify steady RAM usage and matching SHA-256.
- [ ] **USB NTFS Read-Only:** Verify `RETR` succeeds on NTFS partition while `STOR`/`DELE`/`MKD` are rejected with `550`.
- [ ] **Exit & Rescan:** Press `B` to close; verify `FtpServer` stops cleanly and library rescans when files were modified.
