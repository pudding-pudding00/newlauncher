# Changelog

All notable changes to libwiitls are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
follows [Semantic Versioning](https://semver.org/) from 1.0.0 on.

## [Unreleased]

## [1.1.1] - 2026-11-28

### Fixed

- **Retried connects could run out of DNS lookup slots:** a connect that gave up on a running lookup (an error, a
  timeout) left it holding one of the four slots until the resolver answered, and the next attempt started a lookup
  of its own. Quick retries to the same host then failed with `WTLS_ERR_DNS`. A new lookup of that host now takes
  the abandoned one back. This made `test_alloc_fail_nth_connect` fail on macOS, where `localhost` resolves slowly.
- **A non-blocking `wtls_write` could lose data:** when a record had gone out only in part (`WTLS_WANT_WRITE`)
  and the retry passed more data than that record held, the call counted up to a full record as sent although
  only the pending one went out. The peer then waited for bytes that never came. This made
  `test_nonblocking_write_retry_after_read` fail now and then on macOS. Affects 1.0.0 to 1.1.0.

### Changed

- **Releases only from main, and only when its checks passed:** `release.yml` now starts with a gate
  (`tools/release_gate.sh`). The tagged commit must be on main, and every run that its push to main started
  (CI and CodeQL) must have succeeded. The gate waits for those runs if they are still going.

## [1.1.0] - 2026-11-28

### Changed

- **Security – no TLS in Dolphin by default:** in Dolphin the timebase jitter of an IOS round trip is deterministic
  (two alternating values or a constant, ~0 bits of min-entropy per sample in Dolphin 2609b and 2609-324,
  `docs/dolphin.md`) but mostly passes the RCT/APT health tests, so 1.0.0 and 1.0.1 seeded the random generator there from predictable data: keys and TLS secrets of an
  application running in Dolphin can be reconstructed. When `/dev/dolphin` is present, `wtls_init()` and
  `wtls_init_step()` now fail with `WTLS_ERR_ENTROPY`. Real consoles are not affected. For testing in the emulator only,
  the CMake option `WTLS_DOLPHIN_DEV_ENTROPY=ON` (default `OFF`, link test `dolphin_dev_entropy_not_in_release`)
  lets it run anyway with a warning in the log.

### Fixed

- **A connect reported `WTLS_ERR_NOMEM` when all four DNS lookup slots were busy:** lookups that timed out or
  were cancelled keep their slot until the resolver returns. A new lookup then fails with `WTLS_ERR_DNS`.
- **`tools/hwtest.py --suite all` stopped after 120 s:** `--timeout` limited the whole run, which now takes
  longer. It is now how long the runner may stay silent.
- **A failed 200 KiB HTTP/2 upload crashed the test runner** instead of failing the test: the check read the
  body of a response that had none.
- **HTTP/2 was ten times slower than HTTP/1.1 on the Wii:** the client wrote each frame, and each frame's header
  and payload, with its own `send`. IOS does not allow `TCP_NODELAY`, so Nagle held every small write until the
  server's delayed ACK came back, ~62 ms per sequential request. Both HTTP/2 engines now gather a request's frames
  into one write (the blocking one buffers up to one maximum frame and flushes before reading; the non-blocking one
  merges queued frames before each write): ~9 ms per request. 1.0.0 and 1.0.1 are affected. New host test
  `test_h2_engine`, new case in `test_h2s`.
- Tests: the non-blocking HTTP tests keep an error that `wtls_http_start()` returns at once. On the Wii the POST in
  the GOAWAY test now fails inside `start`, and the test read that as success.
- A non-blocking `wtls_write()` that returned `WTLS_WANT_WRITE` could send the same TLS record twice when
  `wtls_read()` ran before the retry and processed a TLS 1.3 session ticket (Mbed TLS flushes the pending output
  there). The peer then saw a corrupted stream; on the Wii this broke HTTP/2 uploads in `wtls_http_start()`. 1.0.0 and 1.0.1
  are affected when an application uses `wtls_set_nonblocking()` and reads between two writes.
- Tests: `wtls_http_start()` may answer `WTLS_WANT_WRITE` while the connect is still running.
- Tests: the SOCKS5 test expects an IPv4 address type when the test server is given as an address.
- Tests: the write-retry test skips on the Wii when the socket never refuses data, and logs the longest write.
- Tests: the async timeout and stream-sharing tests log their timings.
- **HTTP/2 trailers (blocking engine):** trailers of a response were skipped without HPACK decoding, so a trailer that added a dynamic table entry left the client's table out of step with the server's for the next request on that connection (decoding error or wrong header). They are decoded and dropped now.
- **HPACK heap use-after-free (HTTP/2):** a response header block with a
  literal-with-indexing field whose name referred to a dynamic table entry,
  and whose insertion evicted that very entry, copied the name out of freed
  memory. A malicious or broken HTTP/2 server could trigger it. 1.0.0 and
  1.0.1 are affected; found by the new `fuzz_hpack` / `fuzz_h2_frames`
  harnesses.

### Added

- Tests: the fuzz corpus grew by 4,572 inputs from one hour of libFuzzer per harness (all 15, no crashes),
  merged with `-merge=1` so each adds coverage; the corpus regression tests replay them on every host build.
- **Non-blocking DNS on the Wii:** `async_dns` in `wtls_connect_start()`, `wtls_http_start()`, and the abort and
  `dns_timeout_ms` options of `wtls_connect()` now also work on the Wii. The lookups run on one resolver thread
  (priority 80, 16 KiB stack), started on first use; before, the Wii still resolved inside the call. The DNS tests
  of the `tls` and `httpasync` suites now run on the Wii and use a host name, since an address skips the resolver.
- **Non-blocking start-up:** `wtls_init_start()` and `wtls_init_step(budget_ms)` collect the jitter entropy in time slices of 16 samples, so the budget holds to ~1.3 ms on the Wii (also before `net_init()`, e.g. next to `net_init_async()`) and finish like `wtls_init()`; the credited entropy is unchanged. New host test `test_init_step` (the host runs the jitter path with a test sampler), Wii timing in the `entropy` suite.
- Bench: `http_16_small_seq_h1_wall`, `http_16_small_seq_wall` and `http_16_small_async_wall` time 16 small
  requests over HTTP/1.1, sequential HTTP/2 and multiplexed HTTP/2; `docs/benchmarks.md` has the Wii results
  against 1.0.1 and why multiplexing does not win on the Wii (IOS IPC per response).
- Docs: `docs/benchmarks.md` compares `wtls_read_4m` with 1.0.1 on the Wii (seven alternating pairs, median Δ 0.0 %).
- **Dolphin detection:** internal `wtls_plat_is_dolphin()` (`/dev/dolphin`); the `entropy` suite prints `emulator: yes/no`. In Dolphin its raw 64 KiB health-test check logs the outcome instead of requiring a pass (the RCT trips in 2609-324).
- Tests: `tools/hwtest.py --dolphin <Dolphin.exe>` runs a hardware suite in the emulator (`--dolphin-config` for `-C`
  overrides such as the interpreter); the runner fetches its arguments over the log connection, since Dolphin boots a
  DOL without them.
- **Non-blocking HTTP requests:** `wtls_http_start()`, `wtls_http_step()`, `wtls_http_get_fd()`, `wtls_http_pending()` and `wtls_http_wait()` run a request (redirects, cookies, retries, deadlines, gzip, keep-alive pool) as a state machine that never waits; `wtls_http_read()` on such a response returns `WTLS_WANT_READ` instead of waiting. Not supported (`WTLS_ERR_UNSUPPORTED`): proxies, `expect_100_continue`, `allow_upgrade`.
- **HTTP/2 multiplexing in the non-blocking API:** requests started with `wtls_http_start()` to one `https://` host share a single HTTP/2 connection with one stream each (`WTLS_H2_MAX_STREAMS`, default 4, `WTLS_H2_STREAM_WINDOW`, default 32 KiB; both CMake options). Per-stream flow control credits only what the application has read, a closed response cancels just its stream, `GOAWAY`/`REFUSED_STREAM` repeat idempotent requests on a new connection. New engine `wtls_h2s`, new `fuzz_h2s_frames` harness, concurrent HTTP/2 test server (ports 4449/4450).
- HPACK and HTTP/2 frame fuzz harnesses (`fuzz_hpack`, `fuzz_h2_frames`); the
  frame engine now runs over an internal transport interface so it can be
  fuzzed without a socket.
- Status codes `WTLS_ERR_UNSUPPORTED` (-14), `WTLS_ERR_WS` (-53),
  `WTLS_ERR_FILE` (-60) and `WTLS_ERR_INTEGRITY` (-61), used by the 1.1
  features; `wtls_strerror()` describes them.
- Logging: `wtls_set_log_cb()` with levels and categories (platform, handshake,
  certificate, HTTP).
- `wtls_set_keylog_cb()`: TLS key log in the NSS `SSLKEYLOGFILE` format, built
  only with the new `WTLS_KEYLOG` CMake option (default `ON` for Debug builds,
  `OFF` otherwise; release builds return `WTLS_ERR_UNSUPPORTED`).
- `wtls_set_allocator()`: route all library memory (including Mbed TLS and zlib)
  through application-supplied allocator functions, e.g. to use MEM2. The
  Mbed TLS platform memory layer (`MBEDTLS_PLATFORM_MEMORY`) is now enabled.
- Differential libFuzzer harnesses `fuzz_x25519_diff` and `fuzz_p256_verify_diff`
  (accelerators against Everest and the builtin ECDSA), run in CI.
- Project Wycheproof X25519 and ECDSA P-256 vectors in the `crypto` suite
  (`tools/gen_wycheproof.py` regenerates `tests/common/wycheproof_*.inc`).
- Optional `Content-Encoding: br` (`WTLS_BROTLI`) and `zstd` (`WTLS_ZSTD`) decoders, off by default, with window caps (`WTLS_BROTLI_MAX_LGWIN`, `WTLS_ZSTD_MAX_WINDOW_LOG`), `wtls_http_info::br` / `::zstd`, fuzz harnesses; the decoders sit behind a common codec interface.
- `wtls_http_request::total_timeout_ms` (one deadline for connect, redirects, retries and body), `max_retries` / `retry_backoff_ms` / `retry_on_503` (exponential backoff with jitter, Retry-After), idempotent requests only; defaults keep the 1.0 behaviour.
- Proxies: `user:pass@` in `proxy_url` (HTTP Basic Proxy-Authorization for CONNECT and forwarded requests) and `socks5://` / `socks5h://` proxies with optional username/password.
- Cookie jar: `wtls_cookie_jar_new/free/clear/count` and `wtls_http_request::cookie_jar` (RFC 6265 subset, per redirect hop, in memory); `fuzz_set_cookie`.
- Supply chain: `-DWTLS_REPRODUCIBLE=ON` (byte-identical builds, checked in CI on the host toolchain), an SPDX SBOM (`tools/gen_sbom.py`) and build provenance attestations for release packages, deterministic release zip; release process documented in `docs/releasing.md`.
- `wiitls::curl` shim: `READFUNCTION`/`UPLOAD`/`INFILESIZE`, `XFERINFOFUNCTION`, `USERPWD`, more `getinfo` values and a (sequential) multi interface; supported subset documented in `docs/curl-shim.md`.
- `<wiitls/websocket.h>`: WebSocket client (`wtls_ws_connect/send/recv/ping/close`) over `ws://` and `wss://`: framing, client masking, fragmentation, Ping/Pong, close handshake, subprotocols, non-blocking mode; CMake option `WTLS_WEBSOCKET` (default ON). New `fuzz_ws_frame` harness.
- `wtls_time_save()` / `wtls_time_load()` keep the SNTP clock correction across restarts.
- `wtls_http_basic_auth()` and `wtls_http_bearer_auth()` build Authorization header values.
- CMake options `WTLS_SSL_OUT_CONTENT_LEN` and `WTLS_SSL_IN_CONTENT_LEN` to
  shrink the per-connection record buffers (default 16384: unchanged). A
  smaller receive buffer enables the `record_size_limit` / `max_fragment_length`
  extensions and needs servers that honour them.
- `wtls_options::spki_pins_ext` / `n_spki_pins_ext` (up to
  `WTLS_SPKI_MAX_PINS_EXT` = 4 further SPKI pins, six in total); the 1.0
  `spki_pins` array is unchanged. Pin sets stay isolated in the session-ticket
  cache and the HTTP pool.
- `wtls_load_ca_bundle_file()` / `wtls_load_ca_bundle_file_signed()`: load a
  `cacert.pem` from SD/USB at run time (streamed, kept as DER and parsed lazily),
  optionally only when an ECDSA P-256 signature over the file verifies.
- `wtls_http_download()` / `wtls_http_download_recover()`: download to a file
  through `<path>.part` with resume (`Range` / `If-Range`), size limit, SHA-256
  and signature check, progress including the resumed part, and a replacement
  that survives power loss on FAT.
- `<wiitls/sig.h>`: `wtls_sig_verify_p256()` (ECDSA P-256 / SHA-256, raw or
  canonical DER signatures, raw point or SPKI keys), a digest variant and
  incremental `wtls_sha256_*`; `tools/sign_payload.py` creates keys and
  signatures.
- `test_abi`: compile-time guard that the 1.0 layout of `wtls_options`,
  `wtls_http_request` and `wtls_http_info` stays unchanged.

## [1.0.1] - 2026-11-27

### Changed

- Bundled zlib updated from 1.3.1 to 1.4, which includes upstream security and
  bug fixes (see the
  [zlib 1.4 release notes](https://github.com/madler/zlib/releases/tag/v1.4)).

## [1.0.0] - 2026-11-27

Initial public release of **libwiitls** for Nintendo Wii homebrew
(devkitPPC / libogc), built on Mbed TLS 4.1.2 LTS (TF-PSA-Crypto) and
zlib 1.3.1.

### Added

- **HTTP/2 (`h2` ALPN) client support:** `WTLS_HTTP2` CMake option (default
  `ON`), RFC 9113 framing + flow control (`WINDOW_UPDATE`, `SETTINGS`, `PING`,
  `GOAWAY`, `RST_STREAM`), RFC 7541 HPACK (static/dynamic table + Huffman),
  pooled stream reuse, `wtls_http_request::no_h2`, and `wtls_http_info::http2`.
- **Optional/replaceable CA store:** `WTLS_EMBED_CA=OFF` CMake option to omit
  the ~200 KiB embedded Mozilla CA bundle, plus `wtls_reset_trusted_cas()` to
  clear runtime CAs and suppress/restore the built-in roots at runtime.
- **HTTP `101 Switching Protocols` handoff:** `wtls_http_request::allow_upgrade`
  and `wtls_http_detach()` to transfer ownership of the underlying `wtls_conn *`
  (`wss://`) or socket fd (`ws://`) with any early post-head bytes preserved.
- **devkitPro `dkp-pacman` packaging:** `packaging/dkp-wii/PKGBUILD`,
  `tools/make_pkg.sh`, `cmake/wiitls.pc.in`, and `WTLS_INSTALL_ISOLATED_DEPS`
  for conflict-free installation under `$DEVKITPRO/portlibs/wii`.
- **Entropy resilience & diagnostics:** `wtls_jitter_collect_ex()` RCT/APT
  headroom reporting in the `entropy` hardware suite and non-credited
  hardware/network state mixing in `wtls_entropy.c`.
- **Non-blocking connect/handshake:** `wtls_connect_start()` and
  `wtls_connect_step()` for async TCP connect + TLS handshakes.
- **HTTP `deflate`, `Expect: 100-continue`, proxy & ergonomics:**
  - `Content-Encoding: deflate` decompression (`wtls_http_info::deflate`).
  - `wtls_http_request::expect_100_continue` for streaming uploads.
  - Streaming download `progress_cb` support in `wtls_http_open()` /
    `wtls_http_read()`.
  - `wtls_http_form_encode()`, streaming `wtls_http_multipart_*`,
    `wtls_http_format_range()`, and `wtls_http_check_resume()`.
  - `wtls_http_request::proxy_url` (`http://` forward proxy and `https://`
    `CONNECT` tunneling).
- **Password-protected client keys:** `wtls_client_cred_load_ex()` supporting
  encrypted PKCS#8 (PBES2) and encrypted PEM private keys.
- **Optional `libcurl`-lite compatibility shim:** standalone adapter target
  `wiitls::curl` (`adapters/curl_lite/`, enabled via `WTLS_BUILD_CURL_SHIM=ON`).
- **TLS 1.3 & 1.2 client (`<wiitls/wiitls.h>`):**
  - ChaCha20-Poly1305 preferred (fast without AES hardware), AES-GCM
    fallback; X25519, P-256 and P-384 key exchange; ALPN support.
  - Always-on certificate chain and hostname verification (including IPv4
    literals via `iPAddress` SAN) against the embedded Mozilla CA bundle
    (118 roots in `.rodata`, parsed lazily), plus `wtls_add_trusted_ca()` for
    custom roots.
  - Optional SHA-256 SPKI public-key pinning (`spki_pins`, `n_spki_pins`,
    up to 2 pins) against any certificate in the verified chain.
  - Mutual TLS (`wtls_client_cred_load()`, `wtls_client_cred_free()`,
    `client_cred`) for ECDSA and RSA keys in PEM or DER, reference-counted
    and zeroised on release; isolated per identity in the session cache and
    HTTP pool.
  - TLS 1.3 session ticket cache (8 entries) and multi-IPv4 DNS cache
    (16 entries, connect fallback across up to 4 A-records).
  - Blocking I/O with per-stage timeouts and non-blocking mode
    (`wtls_set_nonblocking()`, `wtls_pending()`, `wtls_get_fd()`).
  - Cooperative cancellation (`abort_cb` / `abort_ctx`, `WTLS_ERR_CANCELLED`)
    checked before connect, across handshake steps, and in ≤ 100 ms poll
    slices.
- **HTTP/1.1 client (`<wiitls/http.h>`):**
  - `https://` and `http://` requests with `wtls_http_fetch()` (whole body,
    1 MiB default limit) or streaming `wtls_http_open()` /
    `wtls_http_read()` / `wtls_http_close()`.
  - Streaming uploads (`wtls_http_body_cb`) with `Content-Length` or
    `Transfer-Encoding: chunked`.
  - Built-in gzip decompression (`WTLS_GZIP`, default ON).
  - Keep-alive connection pool (4 idle connections, 60 s) with liveness
    checks and single retry of idempotent methods on stale connections.
  - Redirects (301, 302, 303, 307, 308) with cross-origin stripping of
    `Authorization`, `Proxy-Authorization`, `Cookie` and client
    certificates, and `https` → `http` downgrade refusal.
  - Progress callback (`progress_cb`) for `wtls_http_fetch()`.
  - `wtls_http_url_encode()` and `wtls_http_parse_content_range()` helpers.
- **Wii platform & performance:**
  - 32-bit-limb X25519 (`WTLS_ACCEL`, constant-time) and P-256 ECDSA
    verify (`WTLS_ACCEL_P256`) tuned for the Broadway CPU.
  - IOS IPC timing-jitter entropy source with SP 800-90B RCT/APT health
    tests and SHA-256 conditioning (fails closed with `WTLS_ERR_ENTROPY`).
  - SNTP clock sync (`wtls_time_sync_sntp()`), automatic Wii IP-change
    detection (`net_gethostip()`), `wtls_net_reset()`, and IOS socket
    workarounds (`wtls_plat_connect` polling, 2 KiB send cap).
- **Packaging, safety & tooling:**
  - `WTLS_VERSION_*` macros, `wtls_version()`, and zero-initialised
    append-only options/request structs under SemVer 1.x.
  - Debug single-thread affinity guard (`WTLS_THREAD_CHECK`).
  - CMake `cmake --install` + `find_package(wiitls 1.0 REQUIRED)` exporting
    `wiitls::wiitls`, native Windows Wii toolchain
    (`cmake/wii-win-toolchain.cmake`), and `tools/make_release.sh`.
  - Examples `examples/https_get` and `examples/upload_mtls_abort`.
  - Six Clang libFuzzer harnesses (`tests/fuzz/`), hardware test runner
    (`tests/hw/`), benchmark runner (`bench/`), [security
    review](docs/security-review.md), [security policy](SECURITY.md), and
    monthly CA bundle / Mbed TLS LTS check workflow.

### Fixed

- HTTP/2: following a redirect no longer leaks the previous response's header
  buffer.

[Unreleased]: https://github.com/pudding-pudding00/libwiitls/compare/v1.1.1...HEAD
[1.1.1]: https://github.com/pudding-pudding00/libwiitls/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/pudding-pudding00/libwiitls/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/pudding-pudding00/libwiitls/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/pudding-pudding00/libwiitls/releases/tag/v1.0.0
