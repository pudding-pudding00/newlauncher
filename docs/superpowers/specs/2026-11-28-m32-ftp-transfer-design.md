# M32: Network File Transfer to SD / USB (FTP Server) Design

Status: Draft Spec (2026-11-28) · Tracks: [Issue #5](https://github.com/pudding-pudding00/newlauncher/issues/5)

## 1. Purpose & Goals

Allow users to upload, download, rename, and delete files on any mounted **SD** or **USB** drive directly over the local network using standard FTP clients (FileZilla, WinSCP, Cyberduck, OS file managers, `curl`/`lftp`), without removing storage media from the Wii.

### Scope & Protocol Decisions (Locked)

- **Protocol:** RFC 959 **FTP** in **Passive (`PASV`) mode**, default port **`21`** (configurable via `[network] ftp_port`).
- **Strictly FTP in M32:** No browser web-upload page in M32.
- **SFTP Out of Scope:** SFTP runs over SSH-2 (not TLS) and would require porting/maintaining a full SSH server on libogc. Deferred unless clearly justified later.
- **Explicit Foreground Activation:** The FTP server runs **only while the dedicated Transfer scene is open**—never silently open on the LAN in the background.

## 2. Virtual Filesystem & Security Jail (`src/core/ftp_vfs.{hpp,cpp}`)

All path normalization and access-control logic lives in `src/core/` (zero `<ogc/...>`, 100% host-testable).

### 2.1 Virtual Root Layout

| FTP Path | Target Storage | Writable? |
|---|---|---|
| `/` | Virtual root listing mounted devices (`sd`, `usb`, `usb2`, …) | Never (`550 Permission denied`) |
| `/sd/...` | `sd:/...` | Follows `Device::writable` (true on FAT32) |
| `/usb/...` | `usb:/...` | Follows `Device::writable` (true on FAT32, **false** on read-only NTFS) |
| `/usb2/...` | `usb2:/...` | Follows `Device::writable` |
| *(anything `nand:`)* | **Not mapped — strictly forbidden** | Never exposed |

### 2.2 Path Sanitization Rules

`Result<FtpPath> resolveFtpPath(std::string_view cwd, std::string_view rawArg, std::span<const FtpMount> mounts)`:

- Reject empty/control characters (`< 0x20` or `0x7F`), NUL bytes, backslashes `\`, and colons `:` in path components (prevents raw device injection like `nand:/title/...`).
- Combine relative arguments with `cwd`; resolve `.` and `..` lexically, clamping `..` at `/` (never allowing escape above `/`).
- Validate first path segment matches a currently mounted device name in `mounts` (case-insensitive on mount name, preserved on subpaths).
- Reject attempts to delete, rename, or overwrite a virtual root mount point itself (`/`, `/sd`, `/usb`).
- Enforce `mount.writable == true` on `STOR`, `APPE`, `DELE`, `MKD`, `RMD`, `RNFR`, `RNTO`; return `550 Read-only filesystem` otherwise (critical for `NL_FS_NTFS` read-only mounts).
- Reject cross-device `RNFR`/`RNTO` (`550 Cross-device rename not supported`).

## 3. Core FTP Protocol State Machine (`src/core/ftp_proto.{hpp,cpp}`)

Pure C++ command parser and session state machine with zero socket calls.

### 3.1 Supported Commands

| Category | Commands | Behavior |
|---|---|---|
| Auth | `USER`, `PASS`, `QUIT` | Constant-time password check; lockout after 5 consecutive failures (30 s) |
| Info / Feature | `SYST`, `FEAT`, `OPTS UTF8 ON`, `NOOP`, `TYPE` | `SYST` → `215 UNIX Type: L8`; `FEAT` advertises `UTF8`, `SIZE`, `PASV`, `REST STREAM`, `MLST`, `MLSD`; `TYPE I` and `TYPE A` accepted (`200`) |
| Navigation | `PWD`, `CWD`, `CDUP` | Updates virtual `cwd`; quotes `"` escaped per RFC 959 |
| Transfer Setup | `PASV`, `REST`, `ABOR` | `PASV` opens ephemeral passive port and replies `227 Entering Passive Mode (a,b,c,d,p1,p2)`; `PORT` returns `502 Use PASV` |
| Listing | `LIST`, `NLST`, `MLSD`, `SIZE` | `LIST` emits standard UNIX `-rw-r--r--` / `drwxr-xr-x` lines; `MLSD` emits RFC 3659 `type=file;size=<n>; <name>` lines for unambiguous parsing |
| File I/O | `RETR`, `STOR`, `APPE` | Supports `REST <offset>` resume on `RETR`/`STOR` (reset to 0 after use or error) |
| File Mgmt | `DELE`, `MKD`, `RMD`, `RNFR`, `RNTO` | Full CRUD on writable mounts; marks session `dirty = true` on any successful mutation |

### 3.2 Authentication & Privacy

- Username: configurable `[network] ftp_user` (default `"wii"`).
- Password:
  - Default mode (**ephemeral session PIN**): generates a fresh random 6-digit numeric PIN via `secureRandom` each time the Transfer Scene opens, displayed on screen and embedded in the `ftp://` QR code.
  - Optional configured password: stored only as salted SHA-256 hash (`[network] ftp_pass_hash`, same hashing pattern as `core/parental.hpp`), never plaintext in `config.ini` and **never logged**.
  - `[debug] ftp_pass = <pin>` (from `debug.ini` only, for Dolphin smoke tests).

## 4. Platform FTP Server (`src/platform/ftp_server.{hpp,cpp}`)

### 4.1 Wii IOS Socket Pool Hygiene

Wii IOS exposes a small global socket pool (~16–24 descriptors across all threads). The FTP server strictly caps its socket usage to **at most 3 descriptors simultaneously**:

- `1` control listening socket (port `ftp_port`, default `21`)
- `1` active client control socket (any second simultaneous connection immediately receives `421 Too many connections` and closes)
- `1` passive data listening/accepted socket (closed immediately when transfer finishes, aborts, or a new `PASV` is issued)

### 4.2 Streaming & Memory Budget

- Dedicated LWP thread (`kStackBytes = 64 * 1024`, priority below UI thread), plain libogc sockets (`net_*`, zero `wtls_*`).
- **32 KiB fixed transfer buffer** (`kFtpBufBytes = 32 * 1024`) allocated once on the server thread:
  - Streams arbitrarily large files (e.g., 4 GiB Wii WBFS/ISO files on FAT32/USB) with constant O(1) RAM usage.
  - Checks `stop_` and `ABOR` between chunks; polls with 100 ms slices so `stop()` returns promptly even mid-transfer.
  - Periodic `fflush` / error checks on every `fwrite`; reports `451 Write error` / `452 Insufficient storage` cleanly and closes partial file handles on abort.
- Timeouts:
  - Control idle timeout: `120 s`
  - Passive data accept timeout: `15 s`
  - Data I/O stall timeout: `30 s`

### 4.3 Live Telemetry Snapshot (`FtpStatus`)

Mutex-protected snapshot polled by the UI thread:

- `state`: `Starting | Listening | Connected | Uploading | Downloading | Error | Stopped`
- `listenIp` (u32), `port` (u16), `clientConnected` (bool)
- `currentOp` (`"STOR"`, `"RETR"`, `"LIST"`, etc.), `currentPath` (truncated display path)
- `bytesTransferred` (u64), `speedBytesPerSec` (u32), `filesChanged` (u32)
- `dirty` (bool — true if any write/rename/delete succeeded)

## 5. Transfer UI Scene & Lifecycle (`src/ui/transfer_scene.{hpp,cpp}`)

- **Entry Point:** Accessible from Settings → Network → **File Transfer (FTP)** (and quick-access shortcut in `HomeMenu` or Settings toolbar).
- **Parental Controls:** Requires PIN before opening if parental lock is active.
- **On Enter (`main.cpp` coordination):**
  1. Pause/cancel active background cover downloads or library scans so no concurrent writes race on SD/USB.
  2. Build current `FtpMount` list from `platform::deviceTable()` (only currently mounted SD/USB devices, carrying each device's `writable` flag).
  3. Generate ephemeral session password (unless custom hash configured), construct display URL `ftp://<user>:<pass>@<ip>` (omitting `:21`, including `:<port>` when non-default) and QR code (`core/qr.hpp`).
  4. Start `FtpServer`.
- **On-Screen Display (DE + EN):**
  - Connection info: IP, port, username, session PIN/password (or "Custom password"), `ftp://` URI, QR code (`ui/qr_view`).
  - Mounted drives list with `RW` / `RO` badges (e.g., `/sd (RW)`, `/usb (RW)`, `/usb2 (RO - NTFS)`).
  - Live transfer area: client status, active filename, transferred bytes, speed (KiB/s or MiB/s), and changed-file count.
  - Hint: `B` stops server and closes scene (if a transfer is actively running, show confirmation "Stop active transfer?").
- **On Exit:**
  1. Call `FtpServer::stop()` (closes all sockets and joins thread).
  2. If `dirty == true`, prompt or automatically trigger a library rescan (`scan_worker`) so newly uploaded games, covers, or emulators appear immediately.

## 6. Configuration & Log Markers

- `config.ini [network]`:
  - `ftp_port = 21` (valid `1..65535`, invalid falls back to `21` and records `badKeys`)
  - `ftp_user = wii` (1–32 ASCII alnum/`_-`)
  - `ftp_pass_hash = <hex>` (optional; empty = ephemeral 6-digit session PIN)
- `[debug]` (Dolphin smokes only):
  - `open_scene = transfer`
  - `ftp_port = <u16>`
  - `ftp_pass = <pin>` (fixed session PIN for deterministic smoke login)
- Log markers (never log passwords or user credentials):
  - `NL_FTP listen port=<p>`
  - `NL_FTP auth ok|fail`
  - `NL_FTP xfer op=stor|retr bytes=<n> ok|fail`
  - `NL_FTP mut op=dele|mkd|rmd|rnto ok|fail`
  - `NL_FTP stop dirty=0|1`

## 7. Verification Strategy (No-Wii-First)

1. **`[HOST]`:**
   - `tests/host/core/ftp_vfs_test.cpp`: virtual root listing, `/sd` & `/usb*` mapping, `..` traversal clamping, rejection of `nand:/`, `:` / `\` / control chars, read-only mount enforcement, cross-device rename rejection.
   - `tests/host/core/ftp_proto_test.cpp`: full RFC 959 command parsing, auth state machine + lockout after 5 failures, `PASV` reply formatting, `LIST`/`MLSD` formatting, `REST` offset lifecycle, `RNFR`/`RNTO` pairing, `PORT` rejection.
   - `tests/host/core/config_test.cpp`: `[network] ftp_port`, `ftp_user`, `ftp_pass_hash` round-trip & validation.
   - `tests/host/ui/transfer_scene_test.cpp`: DE+EN rendering, QR visibility, RW/RO drive badges, active-transfer close confirmation, Parental PIN gate.
2. **`[BUILD]`:** 0 warnings on Wii builds with TLS and `--no-tls`.
3. **`[DOLPHIN]`:** Extend `tools/dolphin_smoke.py` (`--ftp-smoke`):
   - Boots `newlauncher` in Dolphin with `[debug] open_scene=transfer`, `ftp_port=2121` (or `21` if unprivileged), `ftp_pass=123456`.
   - Waits for `NL_FTP listen`, connects with Python standard library `ftplib.FTP` to `127.0.0.1`, tests: bad password rejection (`530`), valid login (`230`), `LIST /` (sees `sd`), `MKD /sd/ftp_test`, `STOR` binary file + `RETR` byte-for-byte SHA-256 verification, `RNFR`/`RNTO`, `DELE`, `RMD`, and verifies `CWD /nand` / `RETR /../nand` fails with `550`.
   - Closes scene and verifies `NL_FTP stop dirty=1`.
4. **`[WII-HW]` (Final Hardware Phase Only):** `docs/acceptance/m32-wii.md` — FileZilla & WinSCP passive transfer tests on real Wii over Wi-Fi to SD (FAT32), USB (FAT32, large multi-100 MiB file verifying throughput & constant RAM), read-only USB NTFS rejection, and post-transfer library rescan.
