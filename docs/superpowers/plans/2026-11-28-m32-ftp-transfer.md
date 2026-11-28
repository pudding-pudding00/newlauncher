# M32: Network File Transfer to SD / USB (FTP Server) — Implementation Plan

> **For agentic workers:** Follow TDD (failing test first, then implementation). Complete **all `[HOST]`, `[BUILD]`, and `[DOLPHIN]` tasks first**; defer `[WII-HW]` to the consolidated Stage 6 hardware pass in [`2026-11-28-m30-m34-master-roadmap.md`](2026-11-28-m30-m34-master-roadmap.md).

**Goal:** Add a dedicated foreground **File Transfer (FTP)** scene and RFC 959 passive-mode FTP server (default port **`21`**, strictly FTP — no web-upload page) exposing mounted SD and USB drives safely (read-only NTFS enforced, `nand:/` strictly forbidden), with QR connection info, ephemeral session PIN / hashed credentials, Parental PIN gate, and post-transfer library rescan.

**Spec:** [`docs/superpowers/specs/2026-11-28-m32-ftp-transfer-design.md`](../specs/2026-11-28-m32-ftp-transfer-design.md)

**Branch:** `feat/m32-ftp-transfer`

## Global Constraints
- All FTP parsing, session state machine, authentication, and path-jail logic lives in `src/core/` (zero `<ogc/...>`, no `throw`, no RTTI).
- Strict Wii IOS socket budget: max **3 sockets** total (1 control listen, 1 active control client, 1 passive data socket); 32 KiB streaming transfer buffer (O(1) RAM for multi-GiB files).
- Plain libogc sockets only — never call `wtls_*`; never log passwords or expose `nand:/`.
- 0 warnings on Wii builds with and without TLS; bilingual DE + EN strings in `src/ui/strings.hpp`.

---

### Task 1 `[HOST]`: Virtual Filesystem Path Jail (`core/ftp_vfs`)
**Files:** Create `src/core/ftp_vfs.{hpp,cpp}`; Update `CMakeLists.txt`; Test `tests/host/core/ftp_vfs_test.cpp`
- Produces:
  - `struct FtpMount { std::string name; bool writable = false; };` (e.g., `{"sd", true}`, `{"usb", true}`, `{"usb2", false}`)
  - `struct FtpPath { bool isRoot = false; bool isMountRoot = false; std::string mountName; bool writable = false; std::string virtualPath; std::string fsPath; };`
  - `Result<FtpPath> resolveFtpPath(std::string_view cwd, std::string_view arg, std::span<const FtpMount> mounts);`
  - `bool ftpMutationAllowed(const FtpPath& path);`
  - `bool ftpRenameAllowed(const FtpPath& from, const FtpPath& to);`
- [ ] Write failing unit tests:
  - `/` returns `isRoot = true`, `ftpMutationAllowed == false`.
  - `/sd`, `/sd/`, `/sd/apps/test` map to `sd:/`, `sd:/apps/test`; relative paths resolve against `cwd`; `.` and `..` normalize cleanly and clamp at `/`.
  - Rejects: unknown mount (`/nand`, `/nand/title`), colon injection (`/sd/foo:bar`, `nand:/title`), backslash `\`, control chars / NUL.
  - Read-only mount (`/usb2` with `writable=false`): read allowed, `ftpMutationAllowed == false`.
  - Mount roots (`/sd`, `/usb`): cannot delete/rename (`ftpMutationAllowed == false`).
  - Cross-device rename (`/sd/a` → `/usb/a`) rejected; same-device writable rename allowed.
- [ ] Implement `src/core/ftp_vfs.{hpp,cpp}`, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add FTP virtual filesystem path sanitizer and mount jail`.

### Task 2 `[HOST]`: RFC 959 FTP Command Parser & Session State Machine (`core/ftp_proto`)
**Files:** Create `src/core/ftp_proto.{hpp,cpp}`; Update `CMakeLists.txt`; Test `tests/host/core/ftp_proto_test.cpp`
- Produces:
  - `struct FtpCommand { std::string verb; std::string arg; };`
  - `std::optional<FtpCommand> parseFtpCommand(std::string_view line);`
  - `class FtpSession` managing auth state (constant-time verify + 5-fail 30 s lockout), `cwd`, `restOffset`, `renameFrom`, `dirty` flag, passive IP/port formatting (`formatPasvReply(u32 ip, u16 port)`), `LIST` UNIX line formatting (`formatUnixListLine`), and `MLSD` fact formatting (`formatMlsdLine`).
  - `std::string formatFtpUri(std::string_view user, std::string_view pass, u32 ipHostOrder, u16 port);` (omits `:21`, includes `:<port>` otherwise)
- [ ] Write failing unit tests:
  - Command splitting & case-insensitive verbs (`user`, `PASS`, `CWD /sd/games`, `STOR my file.iso`).
  - Auth flow: commands before login → `530`; bad password → `530`; 5th failure → `421` lockout for 30 s; valid password → `230`.
  - `SYST` (`215 UNIX Type: L8`), `FEAT`, `OPTS UTF8 ON`, `PWD` (with `"` escaping), `CWD`, `CDUP`, `TYPE I/A`, `PORT` → `502 Use PASV`, `PASV` reply `(a,b,c,d,p1,p2)`, `REST <n>` consumed once by next transfer, `RNFR`/`RNTO` pairing & reset on error.
  - `LIST` and `MLSD` formatting for virtual root mounts and regular files/dirs.
  - `formatFtpUri` omits `:21` and includes `:2121`.
- [ ] Implement `src/core/ftp_proto.{hpp,cpp}`, register in `CMakeLists.txt`, verify `ctest --preset host` green.
- [ ] Commit: `feat(core): add RFC 959 FTP protocol parser and session state machine`.

### Task 3 `[HOST]`: Config Keys, Credentials Hashing & Transfer UI Scene
**Files:** Modify `src/core/config.{hpp,cpp}`, `src/core/settings_model.{hpp,cpp}`, `src/ui/strings.{hpp,cpp}`, `src/ui/app_command.hpp`, `src/ui/settings_scene.{hpp,cpp}`; Create `src/ui/transfer_scene.{hpp,cpp}`; Tests `tests/host/core/config_test.cpp`, `tests/host/ui/transfer_scene_test.cpp`
- [ ] Write failing tests for:
  - `[network] ftp_port = 21` (range `1..65535`), `ftp_user = wii`, `ftp_pass_hash` (salted SHA-256 hash, never plaintext); `[debug] ftp_port`, `[debug] ftp_pass`, `open_scene = transfer`.
  - `TransferScene`: displays IP, port, URI, QR code (`drawQr`), ephemeral PIN or custom-password indicator, mounted drives (`RW` / `RO` badges), active transfer stats (filename, bytes, KiB/s), close confirmation when transfer active, `B` exits cleanly, DE + EN strings complete.
  - Parental Controls PIN gate before opening `TransferScene` when parental lock is active.
- [ ] Implement config, settings entry, bilingual strings, and `TransferScene`; verify `ctest --preset host` green.
- [ ] Commit: `feat(ui): add FTP settings and foreground File Transfer scene`.

### Task 4 `[BUILD]`: Platform Passive FTP Server (`platform/ftp_server`) & `main.cpp` Wiring
**Files:** Create `src/platform/ftp_server.{hpp,cpp}`; Modify `src/app/main.cpp`, `CMakeLists.txt`
- [ ] Implement `platform::FtpServer`:
  - LWP thread (`64 KiB` stack), plain `net_*` sockets, strict max 3 descriptors (1 listen, 1 active client + immediate `421` overflow reject, 1 passive data socket).
  - Fixed 32 KiB streaming buffer for `RETR` / `STOR` / `APPE` directly to/from `FILE*` (respecting `REST` offset, checking `fwrite` errors / disk full, polling in 100 ms slices for `stop_` and `ABOR`).
  - `LIST` / `NLST` / `MLSD` / `SIZE` / `DELE` / `MKD` / `RMD` / `RNFR` / `RNTO` wired through `core/ftp_vfs` + `core/ftp_proto` + `std_fs`.
  - Thread-safe `status()` returning `FtpStatus` telemetry snapshot (state, op, path, bytes, speed, `dirty`), `takeLogLines()` emitting `NL_FTP ...` markers (never logging password).
- [ ] Wire `TransferScene` lifecycle in `src/app/main.cpp`:
  - Cancel/pause background cover/scan workers on enter; build `FtpMount` list from `platform::deviceTable()` (strictly SD/USB, never NAND); generate ephemeral 6-digit PIN via `secureRandom` (or use `debugFtpPass` / `ftp_pass_hash`); start `FtpServer`.
  - Poll `FtpServer::status()` each frame to update `TransferScene`.
  - On exit: `FtpServer::stop()` (join thread, release all sockets), and if `dirty == true`, prompt/trigger library rescan.
- [ ] Build Wii targets: `python tools/build_wii.py` AND `python tools/build_wii.py --no-tls` — verify **0 warnings** on both.
- [ ] Commit: `feat(platform): add passive FTP server for SD and USB transfers`.

### Task 5 `[HOST]` `[DOLPHIN]`: Dolphin FTP End-to-End Smoke & Docs
**Files:** Modify `tools/dolphin_smoke.py`, `tools/tests/test_dolphin_smoke.py`, `docs/user-guide.md`, `docs/STATUS.md`; Create `docs/acceptance/m32-wii.md`
- [ ] Add `--ftp-smoke` to `tools/dolphin_smoke.py` (with unit tests in `tools/tests/test_dolphin_smoke.py`):
  - Launches `newlauncher.dol` with `[debug] open_scene=transfer`, `ftp_port=2121` (avoids host privileged-port conflicts in CI/Dolphin), `ftp_pass=246810`.
  - Waits for `NL_FTP listen`, then drives a real `ftplib.FTP` passive session against `127.0.0.1:2121`:
    1. Wrong password → `530` (`ftplib.error_perm`)
    2. Valid login (`wii` / `246810`) → `230`
    3. `LIST /` includes `sd` and does **not** include `nand`
    4. `CWD /nand` and `RETR /../nand/title` fail with `550`
    5. `MKD /sd/ftp_smoke`, `STOR` 128 KiB binary payload, `SIZE`, `RETR` & verify SHA-256 match
    6. `RNFR` / `RNTO`, `DELE`, `RMD`
    7. Quit and verify `NL_FTP stop dirty=1` (and confirm password never appears in Dolphin log!).
- [ ] Run `python tools/dolphin_smoke.py --ftp-smoke` + `python -m unittest discover -s tools/tests` — verify green.
- [ ] Write `docs/acceptance/m32-wii.md` (for Stage 6 `[WII-HW]` pass) and update `docs/user-guide.md`.
- [ ] Commit: `test(m32): add Dolphin FTP smoke and user documentation`.
- [ ] Open PR, wait for CI (`gh run watch <id> --exit-status`), merge once green.

### Deferred to Stage 6 `[WII-HW]` (Do NOT Run Yet)
- [ ] Execute `docs/acceptance/m32-wii.md` on real Wii during the consolidated Stage 6 hardware pass.
