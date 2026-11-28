# M34: Code Scanning Remediation (All Open CodeQL Alerts) Design

Status: Draft Spec (2026-11-28) · Tracks: [Issue #5](https://github.com/pudding-pudding00/newlauncher/issues/5)

## 1. Purpose & Goals

Resolve **all 17 open GitHub Code Scanning (CodeQL) alerts** (`#1`–`#17` as of 2026-11-28 on `main`) so the repository achieves **0 open Code Scanning alerts**, and prevent regressions via unit tests and clean vendoring rules.

This milestone is **100% PC-verifiable (`[HOST]` + `[BUILD]` + `[DOLPHIN]`)** and requires **zero real Wii hardware**, so it is prioritized **first** in the No-Wii-First master execution order.

## 2. Inventory of Open Alerts & Root-Cause Fixes

### 2.1 Python Tooling — Insecure TLS Protocol (`#5`, `#6`, `#7`, `#8` · High)

- **Alerts:**
  - `#5` `tools/dolphin_smoke.py:219` (`py/insecure-protocol`)
  - `#6` `tools/cloud_fake.py:967` (`py/insecure-protocol`)
  - `#7` `tools/tests/test_test_ca.py:57` (`py/insecure-protocol`)
  - `#8` `tools/tests/test_test_ca.py:83` (`py/insecure-protocol`)
- **Root Cause:** `ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)` and `ssl.create_default_context()` without an explicit `minimum_version` are flagged by CodeQL as potentially permitting TLS 1.0 / 1.1.
- **Fix:**
  - Explicitly set `ctx.minimum_version = ssl.TLSVersion.TLSv1_2` in:
    - `ssl_server_context()` in `tools/dolphin_smoke.py`
    - `start()` (or helper enforcing minimum version on passed/created `ssl_context`) in `tools/cloud_fake.py`
    - Both `server_ctx` and `client_ctx` in `tools/tests/test_test_ca.py`
  - Matches `libwiitls`'s own minimum protocol version (TLS 1.2).

### 2.2 Python Tooling — HTTP Response Splitting (`#3` · Medium)

- **Alert:** `#3` `tools/cloud_fake.py:348` (`py/http-response-splitting`)
- **Root Cause:** `Handler._send()` forwards header values from `headers` directly into `self.send_header(k, v)` where values may incorporate incoming request parameters/headers.
- **Fix:**
  - Add a strict header sanitizer/validator helper in `tools/cloud_fake.py` called by `_send()`:
    - Validate header names match RFC 9110 token characters (`^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$`).
    - Reject any header value containing `\r`, `\n`, `\x00`, or other control characters (`ord(c) < 0x20 and c != '\t'` or `ord(c) == 0x7F`) by raising `ValueError` (returning HTTP 400 where appropriate).
  - Add unit tests in `tools/tests/test_cloud_fake.py` verifying CRLF injection attempts are rejected.

### 2.3 Python Tooling — Clear-Text Sensitive Data Storage & Logging (`#1`, `#2` · High)

- **Alerts:**
  - `#1` `tools/dolphin_smoke.py:615` (`py/clear-text-storage-sensitive-data`)
  - `#2` `tools/dolphin_smoke.py:716` (`py/clear-text-logging-sensitive-data`)
- **Root Cause:** CodeQL tracks taint from synthetic test-only OAuth fixture fields (e.g., variable/key names matching `*secret*` / `*token*`) in `tools/cloud_smoke.py` through `plan.config_append` (written to the temporary Dolphin virtual SD `config.ini` at line 615) and through `missing` diagnostic strings printed to stdout at line 716.
- **Fix:**
  - Inspect the exact taint flow from `tools/cloud_smoke.py` into `tools/dolphin_smoke.py`:
    - Ensure `cloud_smoke.post_check()` and `_run_dolphin()` **never** embed credential/token/secret values in returned `missing` failure messages—only fixed marker names or field labels (`"missing token file"`, `"bad marker: NL_..."`).
    - Separate the synthetic Dolphin fake-server OAuth test config writer cleanly (avoid carrying live/sensitive-named secret fields in general log/config pipelines; use explicit non-production mock fixture constants clearly scoped to the local fake server and scrub/delete the temporary SD config on cleanup).
  - Add regression tests in `tools/tests/test_dolphin_smoke.py` verifying failure messages never echo credential values.

### 2.4 Vendored `libwiitls` Unused Tool (`#4` · Medium)

- **Alert:** `#4` `third_party/wii_tls/tools/hwtest.py:60` (`py/bind-socket-all-network-interfaces`)
- **Root Cause:** `tools/vendor_wii_tls.py` strips `tests`, `bench`, `examples`, and `docs`, but did not strip `tools/`—leaving upstream `libwiitls` test-runner scripts vendored in `third_party/wii_tls/tools/` even though `newlauncher` never executes or builds them.
- **Fix:**
  - Add `"tools"` to `DROP_DIRS` in `tools/vendor_wii_tls.py` (updating `tools/tests/test_vendor_wii_tls.py`).
  - Remove the unused `third_party/wii_tls/tools/` directory (and verify `build_wii.py` + `check_licenses.py` stay completely green).
  - Ensures future re-vendors (including M33 `libwiitls v1.1.0`) never reintroduce upstream dev scripts.

### 2.5 First-Party C / C++ — Format Specifier & Integer Overflow (`#13`, `#16`, `#17` · High)

- **Alert `#13` (`tests/host/support/wbfs_part_image.hpp:66` · `cpp/integer-multiplication-cast-to-long`):**
  - `(bytes + ss - 1) / ss * ss` performs 32-bit `u32` arithmetic before converting to `std::vector<u8>::size_type`.
  - **Fix:** Widen to `std::size_t` before arithmetic: `const std::size_t rounded = (std::size_t{bytes} + ss - 1) / ss * ss; std::vector<u8> out(rounded);`.
- **Alerts `#16` (`boot/source/patchcode.c:1504`) & `#17` (`boot/source/patchcode.c:855`) · `cpp/wrong-type-format-argument`:**
  - `gprintf("... 0x%x to 0x%x\n", offset, patch);` passes pointers (`void *` / `u8 *`) to `%x`.
  - **Fix:** Cast explicitly via `<stdint.h>`: `(unsigned int)(uintptr_t)offset, (unsigned int)(uintptr_t)patch` so the format specifier matches the argument type while preserving identical 32-bit Wii address formatting.

### 2.6 Vendored `doctest` — Real Self-Append Use-After-Free (`#9` · Critical)

- **Alert:** `#9` `third_party/doctest/doctest.h:3705` (`cpp/use-after-free`)
- **Root Cause:** In `doctest::String::operator+=`, when `this == &other`, `delete[] data.ptr;` at line 3700 frees the source buffer before `memcpy(data.ptr + my_old_size, other.c_str(), other_size + 1);` at line 3705 reads it!
- **Fix:**
  - First check if the latest upstream `doctest` release fixes this cleanly (if so, update `third_party/doctest/doctest.h` and `THIRD_PARTY.md`).
  - Otherwise apply the minimal safe fix directly in `third_party/doctest/doctest.h`: copy `other.c_str()` into `temp + my_old_size` **before** calling `delete[] data.ptr;` and record the patch note in `THIRD_PARTY.md`.

### 2.7 Vendored `libntfs` — Pointer-Lifetime & Integer-Overflow Fixes (`#10`, `#11`, `#12` · Critical; `#14`, `#15` · High)

- **Alerts `#10` (`inode.c:673`), `#11` (`mft.c:1921`), `#12` (`mft.c:1595`) · `cpp/use-after-free`:**
  - All three sites reallocate `base_ni->extent_nis` by calling `free(base_ni->extent_nis);` before overwriting `base_ni->extent_nis = extent_nis;`.
  - **Fix:** Swap the pointer before freeing the old buffer:
    ```c
    ntfs_inode **old_extents = base_ni->extent_nis;
    base_ni->extent_nis = extent_nis;
    if (base_ni->nr_extents)
        free(old_extents);
    ```
    (after `memcpy` from `old_extents`). Eliminates any dangling-pointer window and clears all three CodeQL alerts cleanly.
- **Alerts `#14` (`cache2.c:89`) & `#15` (`mft.c:184`) · `cpp/integer-multiplication-cast-to-long`:**
  - `cache2.c:89`: `ntfs_align(sectorsPerPage * bytesPerSector)` multiplies two `uint32_t` values before passing to `size_t`. Fix: `(size_t)sectorsPerPage * (size_t)bytesPerSector`.
  - `mft.c:184` (and `187`): `cnt * vol->mft_record_size` multiplies 32-bit integers before `size_t`. Fix: `(size_t)cnt * (size_t)vol->mft_record_size` (with overflow guard if `cnt` is large).
- Document all local `third_party/libntfs` patches in `third_party/libntfs/PATCHES.md` (and preserve/apply them cleanly if `tools/vendor_fs.py` is ever re-run).

## 3. Acceptance Criteria

1. **`[HOST]`:**
   - `cmake --preset host && cmake --build --preset host && ctest --preset host` passes.
   - `python -m unittest discover -s tools/tests` passes (including new tests for CRLF header rejection, `vendor_wii_tls.py` dropping `tools/`, and smoke error messages without secrets).
2. **`[BUILD]`:**
   - `python tools/build_wii.py` (TLS + NTFS) compiles with **0 warnings**.
   - `python tools/build_wii.py --no-tls` compiles with **0 warnings**.
   - `python tools/check_licenses.py build/wii` passes.
3. **`[DOLPHIN]`:**
   - Existing Dolphin smokes (`--cloud-fake`, `--sd-disk-layout`, `--cover-url`) remain green with TLS 1.2+ enforced on local test servers.
4. **GitHub Code Scanning Gate (PC / CI):**
   - After merging to `main` (and CodeQL re-run completes), `gh api repos/pudding-pudding00/newlauncher/code-scanning/alerts?state=open` returns **`[]` (0 open alerts)**.
5. **`[WII-HW]`:** None required (all fixes are verified on host, cross-build, Dolphin, and CodeQL CI).
