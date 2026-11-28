# M34: Code Scanning Remediation — Implementation Plan

> **For agentic workers:** Follow TDD (write/add failing regression test first where applicable, then fix). Check off each step (`- [ ]` → `- [x]`). **Zero real Wii hardware required for this entire milestone.**

**Goal:** Fix all **17 open GitHub Code Scanning (CodeQL) alerts** (`#1`–`#17`) on `main`, add regression tests so they cannot return, and reach **0 open Code Scanning alerts** after merge.

**Spec:** [`docs/superpowers/specs/2026-11-28-m34-code-scanning-remediation-design.md`](../specs/2026-11-28-m34-code-scanning-remediation-design.md)

**Branch:** `fix/m34-code-scanning`

## Global Constraints
- Follow `AGENTS.md` (English commits/comments, 0 warnings on Wii builds with and without TLS, no private data in repo).
- Never silence alerts by disabling CodeQL rules or using broad suppressions when a real fix exists.

---

### Task 1 `[HOST]`: Enforce TLS 1.2+ in Python Test/Smoke Servers (Alerts `#5`, `#6`, `#7`, `#8`)
**Files:** Modify `tools/dolphin_smoke.py`, `tools/cloud_fake.py`, `tools/tests/test_test_ca.py`; Test `tools/tests/test_dolphin_smoke.py`, `tools/tests/test_cloud_fake.py`, `tools/tests/test_test_ca.py`
- [ ] Write/extend unit tests asserting that `dolphin_smoke.ssl_server_context()` returns an `ssl.SSLContext` with `minimum_version == ssl.TLSVersion.TLSv1_2` and that `cloud_fake.start()` enforces `minimum_version >= ssl.TLSVersion.TLSv1_2` on its TLS socket context. Verify tests fail before edits.
- [ ] Set `ctx.minimum_version = ssl.TLSVersion.TLSv1_2` in `tools/dolphin_smoke.py` (`ssl_server_context`), `tools/cloud_fake.py` (where `ssl_context` is created/used before `wrap_socket`), and both `server_ctx` & `client_ctx` in `tools/tests/test_test_ca.py`.
- [ ] Run `python -m unittest discover -s tools/tests -v` — verify green.
- [ ] Commit: `fix(tools): enforce TLS 1.2 minimum in smoke and test SSL contexts`.

### Task 2 `[HOST]`: Sanitize HTTP Response Headers in `cloud_fake.py` (Alert `#3`)
**Files:** Modify `tools/cloud_fake.py`; Test `tools/tests/test_cloud_fake.py`
- [ ] Write failing unit tests in `tools/tests/test_cloud_fake.py` verifying that header names/values containing `\r`, `\n`, or NUL/control characters passed to `_send()` / header builder are rejected with `ValueError` (never emitted into raw HTTP headers).
- [ ] Add `_validate_header(name: str, value: str) -> tuple[str, str]` in `tools/cloud_fake.py` checking RFC 9110 header name token chars and forbidding CR/LF/NUL/CTL in values; call it inside `Handler._send()` before `self.send_header(k, v)`.
- [ ] Run `python -m unittest discover -s tools/tests -v` — verify green.
- [ ] Commit: `fix(tools): validate response headers in cloud_fake to prevent CRLF injection`.

### Task 3 `[HOST]`: Remove Clear-Text Secret Storage/Logging Taint in Dolphin Smoke Harness (Alerts `#1`, `#2`)
**Files:** Inspect & modify `tools/cloud_smoke.py`, `tools/dolphin_smoke.py`; Test `tools/tests/test_dolphin_smoke.py`, `tools/tests/test_cloud_fake.py`
- [ ] Inspect exact dataflow from `tools/cloud_smoke.py` into `tools/dolphin_smoke.py:615` (`f.write(plan.config_append)`) and `:716` (`print(... missing ...)`).
- [ ] Write failing test in `tools/tests/test_dolphin_smoke.py` verifying failure messages returned by `cloud_smoke.post_check()` and printed by `_run_dolphin()` never contain credential/token/secret strings.
- [ ] Refactor `cloud_smoke.py` / `dolphin_smoke.py`:
  - Return only fixed diagnostic labels / marker names in `missing` (never raw token/secret values).
  - Decouple local fake-server Dolphin test fixture INI generation from any tainted secret variable names/logging paths, and guarantee cleanup removes temporary test config entries.
- [ ] Run `python -m unittest discover -s tools/tests -v` — verify green.
- [ ] Commit: `fix(tools): eliminate clear-text secret taint in Dolphin smoke runner`.

### Task 4 `[HOST]` `[BUILD]`: Drop Unused `libwiitls/tools/` From Vendoring (Alert `#4`)
**Files:** Modify `tools/vendor_wii_tls.py`, `tools/tests/test_vendor_wii_tls.py`; Delete `third_party/wii_tls/tools/`
- [ ] Add failing test in `tools/tests/test_vendor_wii_tls.py` asserting that `vendor_wii_tls.py` strips `tools/` alongside `tests`, `bench`, `examples`, and `docs`.
- [ ] Add `"tools"` to `DROP_DIRS` in `tools/vendor_wii_tls.py`, remove `third_party/wii_tls/tools/`, and update `third_party/wii_tls/VENDORED.txt` `removed=` line.
- [ ] Run `python -m unittest discover -s tools/tests -v` and `python tools/build_wii.py` + `python tools/check_licenses.py build/wii` — verify 0 warnings and green.
- [ ] Commit: `chore(deps): strip unused upstream tools directory from vendored wii_tls`.

### Task 5 `[HOST]` `[BUILD]`: Fix First-Party C/C++ Alerts (`#13`, `#16`, `#17`)
**Files:** Modify `tests/host/support/wbfs_part_image.hpp`, `boot/source/patchcode.c`
- [ ] In `tests/host/support/wbfs_part_image.hpp:66`, widen arithmetic to `std::size_t` before multiplying:
  `const std::size_t rounded = (std::size_t{bytes} + ss - 1) / ss * ss; std::vector<u8> out(rounded);`
- [ ] In `boot/source/patchcode.c:855` and `:1504`, include `<stdint.h>` if not already present and cast pointer arguments to `(unsigned int)(uintptr_t)offset, (unsigned int)(uintptr_t)patch` to match `0x%x`.
- [ ] Run `cmake --preset host && cmake --build --preset host && ctest --preset host` and `python tools/build_wii.py` + `python tools/build_wii.py --no-tls` — verify 0 warnings.
- [ ] Commit: `fix: resolve format-specifier and integer-widening CodeQL alerts`.

### Task 6 `[HOST]`: Fix Real Self-Append Use-After-Free in Vendored `doctest` (Alert `#9`)
**Files:** Modify `third_party/doctest/doctest.h`, `THIRD_PARTY.md`; Test `tests/host/main.cpp` (or small `doctest_self_append_test.cpp`)
- [ ] Check whether an upstream `doctest` release (2.4.12+) fixes `String::operator+=` self-append (`s += s`).
- [ ] Add a host test exercising `doctest::String s("012345678901234567890123456789"); s += s;` (exceeding SSO buffer to hit line 3705).
- [ ] Either upgrade `third_party/doctest/doctest.h` to the fixed upstream release or patch `String::operator+=` so `memcpy(temp + my_old_size, other.c_str(), other_size + 1);` runs **before** `delete[] data.ptr;`, and note the patch/version in `THIRD_PARTY.md`.
- [ ] Run `cmake --preset host && cmake --build --preset host && ctest --preset host` — verify green.
- [ ] Commit: `fix(third_party): fix self-append use-after-free in vendored doctest`.

### Task 7 `[BUILD]`: Fix Pointer-Swap and Integer-Widening Alerts in Vendored `libntfs` (`#10`, `#11`, `#12`, `#14`, `#15`)
**Files:** Modify `third_party/libntfs/upstream/source/inode.c`, `third_party/libntfs/upstream/source/mft.c`, `third_party/libntfs/upstream/source/cache2.c`; Create `third_party/libntfs/PATCHES.md`
- [ ] In `inode.c:666–673`, `mft.c:1588–1595`, and `mft.c:1914–1921`: save `ntfs_inode **old_extents = base_ni->extent_nis;`, copy from `old_extents`, assign `base_ni->extent_nis = extent_nis;`, then `free(old_extents);` (never leaving `base_ni->extent_nis` dangling).
- [ ] In `cache2.c:89`: widen multiplication to `(size_t)sectorsPerPage * (size_t)bytesPerSector`.
- [ ] In `mft.c:184` (and `187`): widen multiplication to `(size_t)cnt * (size_t)vol->mft_record_size`.
- [ ] Document all 5 patches in `third_party/libntfs/PATCHES.md`.
- [ ] Run `python tools/build_wii.py`, `python tools/build_wii.py --no-tls`, and `python tools/check_licenses.py build/wii` — verify 0 warnings.
- [ ] Commit: `fix(third_party): patch libntfs pointer-swap and size_t widening alerts`.

### Task 8 `[DOLPHIN]` + CI Verification Gate
- [ ] Run representative Dolphin smokes verifying TLS 1.2+ test servers and NTFS still pass:
  - `python tools/dolphin_smoke.py --sd-disk-layout multi` (NTFS + FAT)
  - `python tools/dolphin_smoke.py --cloud-fake drive` (TLS 1.2+ fake server)
- [ ] Push `fix/m34-code-scanning`, open PR referencing Issue #5, wait blockingly with `gh run watch <id> --exit-status`.
- [ ] After merge, wait for CodeQL on `main` and verify `gh api repos/pudding-pudding00/newlauncher/code-scanning/alerts?state=open` returns **`[]` (0 open alerts)**.
