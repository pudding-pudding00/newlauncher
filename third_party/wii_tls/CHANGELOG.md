# Changelog

## 1.0.0 (2026-11-25)

First stable SemVer release of `libwiitls` for Wii homebrew (devkitPPC / libogc),
built on Mbed TLS 4.1.1 LTS (TF-PSA-Crypto) and zlib 1.3.1.

Verified across host Release (`30/30`), host Debug (`WTLS_THREAD_CHECK=1`),
ASan/UBSan/LSan, Clang libFuzzer (6 harnesses), Windows UCRT64 + Ninja, Wii
CMake install/consumer checks, and real Wii hardware over Wi-Fi (`time` 1/1,
`plat` 13/13, `crypto` 14/14, `tls` 40/40, `http` 86/86, `soak` 128 iterations
with 0 heap growth, `realworld` 15/15, `wtls_bench.dol` 10/10 with
`handshake_full_cpu = 27.3 ms`). Note: Wii IOS jitter entropy has been verified
on one retail Wii over Wi-Fi; additional console revisions / USB-Ethernet
`jitterdump` reports are tracked as 1.0.x follow-up.

### Added

- Cooperative cancellation (`wtls_abort_cb`, `abort_ctx` in `wtls_options`,
  `WTLS_ERR_CANCELLED = -13`): checked before connect, across handshake steps,
  at <= 100 ms `net_poll`/`poll` slices (`WTLS_POLL_SLICE_MS`), in HTTP
  `plain_read`/`plain_write`, between HTTP body callbacks/reads, and on
  `wtls_http_open` / `wtls_http_fetch` / `wtls_http_read`. Aborted TLS skips
  `close_notify`; aborted HTTP connections are never returned to the pool.
- HTTP download progress callback (`wtls_http_progress_cb`, `progress_cb` /
  `progress_ctx` in `wtls_http_request`) for `wtls_http_fetch`.
- Stalled-peer write cap (`OGC_SEND_MAX = 2048` B on Wii in `src/plat/plat_ogc.c`
  after hardware probing showed `FIONSPACE`/`FIONWRITE = -22`, `-EAGAIN` corruption
  under `IOS_O_NONBLOCK` down to `1024 B`, and clean `WANT_WRITE` at `73728 B`
  with `2048 B` alignment) plus `test_stalled_peer_nonblocking` and
  `[12] spike_stalled_peer_send`.
- Multi-IPv4 resolution and TCP connect fallback (up to 4 A-records in
  `src/wtls_dns.c`; only the confirmed working address is cached).
- Public `wtls_net_reset()` and automatic host-IP-change detection
  (`net_gethostip()` delta on Wii) to flush pooled connections and DNS entries
  after Wi-Fi/interface transitions without forcing HTTP linkage on TLS-only apps.
- Single-thread enforcement guard (`WTLS_THREAD_CHECK`, always ON in `Debug`
  builds, `LWP_GetSelf()` on Wii / OS thread ID on host; `test_thread`).
- Dynamic Version API (`WTLS_VERSION_MAJOR/MINOR/PATCH` and `wtls_version()`).
- Optional SHA-256 SPKI public-key pinning (`spki_pins[2][32]`, `n_spki_pins`
  in `wtls_options`, `WTLS_SPKI_SHA256_LEN`, `WTLS_SPKI_MAX_PINS`) verified
  against leaf, intermediate, or root SPKI with identity isolation in the
  session cache and HTTP keep-alive pool.
- Six Clang libFuzzer harnesses (`tests/fuzz/`, enabled with
  `-DWTLS_BUILD_FUZZ=ON`) plus deterministic CTest corpus regressions
  (`test_fuzz_*`) executed on every host build.
- Security audit (`docs/security-review.md`), vulnerability disclosure policy
  (`SECURITY.md`), and monthly CA bundle / Mbed TLS LTS check workflow
  (`.github/workflows/maintenance.yml`).
- Soak/endurance test suite (`tests/common/soak_tests.c`, `test_soak` on host,
  `suite=soak [iters=N]` on Wii) and expanded `realworld` server/CDN probes
  (Fastly, Let's Encrypt, nginx, Caddy, GitHub CDN redirect + `Range: 206`).
- CMake install package (`cmake --install`, `find_package(wiitls)` exporting
  `wiitls::wiitls`), official native Windows Wii toolchain
  (`cmake/wii-win-toolchain.cmake`), and second example
  `examples/upload_mtls_abort`.

### Changed

- `wtls_http_build_head()` starts at 512 B and doubles on demand up to
  `WTLS_HTTP_MAX_HEAD` instead of allocating 16 KiB upfront per request hop.
- Streaming upload buffer (`UPLOAD_BUF_SIZE = 16 KiB` in `send_body_cb()`) is
  heap-allocated instead of living on Wii LWP network-thread stacks.
- Interim HTTP `1xx` header loops are capped at 16 responses (`WTLS_ERR_HTTP_LIMIT`).
- `cmake --install` suppresses Mbed TLS's own header/package install rules so
  installed prefixes contain only `libwiitls`, its bundled static archives,
  headers, and `lib/cmake/wiitls/*`.

### Fixed

- Removed duplicate step in `pt_jadd` (`src/accel/p256_verify.c`), zeroized
  scratch buffers in `x25519_base` (`src/accel/x25519_fe32.c`),
  `wtls_jitter_collect`, and `wtls_scache` (`mbedtls_platform_zeroize`),
  guarded empty cert chains (`src/wtls_cred.c`), avoided `NULL + 0` pointer
  arithmetic and missing NULL guards in `src/http/` (including `headers == NULL`
  with `n_headers > 0` in `wtls_http_open()` / `wtls_http_headers_have()`),
  handled 0-byte fixed-length streaming uploads cleanly (`body_cb != NULL`,
  `body_len == 0`, `!body_chunked`), ensured all HTTP `abort_cb` cancellation
  paths skip `mbedtls_ssl_close_notify()`, stopped plain `http://` hops from
  partitioning their keep-alive pool by TLS pins/credentials, guarded `size_t`
  overflow in `wtls_http_url_encode()`, prevented `uint64_t` overflow in
  `cl_elem`, and preserved sticky I/O errors in `wtls_write`.

## 0.3.0 (2026-11-26)

### Added

- P-256 ECDSA verify with 32-bit limbs (`WTLS_ACCEL_P256`, default ON,
  `src/accel/p256_verify.c`), hooked into the builtin driver's
  `mbedtls_psa_ecdsa_verify_hash` at configure time. Public inputs only, so
  it is variable time by design: Shamir's trick over interleaved wNAF
  (w = 6 against the generated affine fixed-base table `k_p256_g_odd`,
  w = 5 against eight odd multiples of the peer key, normalised with a
  single mod-p inversion), Jacobian arithmetic with a = -3 (3M + 5S doubling,
  8M + 3S mixed addition), Solinas field reduction over 8x32 limbs (the
  shifts are whole limbs) and a projective final comparison that covers the
  x_R = r + n wrap-around without a field inversion. Matches the builtin's
  psa_status_t mapping exactly; r/s range, point-on-curve, SEC 1 4.1.4
  hash truncation for 20/32/48/64-byte hashes and the wrap-around case are
  pinned by `tests/common/crypto_tests.c` (RFC 6979 A.2.5 vectors, a
  generated wrap-around vector, and differential tests against
  `mbedtls_ecdsa_verify` on random keys with bit-flip mutations, host and
  Wii). `tools/gen_p256.py` regenerates the curve constants and the
  fixed-base table (checked in as `src/accel/p256_curve.inc`).
  Measured on a real Wii (2026-11-26): `ecdsa_p256_verify`
  25.7 → **5.3 ms**, `handshake_full_cpu` 66.4 → **27.8 ms** — the
  `<= 30 ms` target from 0.1 is met.
- HTTP streaming uploads (`wtls_http_body_cb`, `<wiitls/http.h>`): a
  request body produced by a callback, sent either with `Content-Length`
  (`body_len`) or as `Transfer-Encoding: chunked` (`body_chunked`, the
  callback ends the body by returning 0). A reader that stops early or
  fails aborts with `WTLS_ERR_IO` and the connection is closed, never
  pooled; the stale-connection retry happens only if the callback has not
  been called yet and the method is idempotent; 303 (and 301/302 after
  POST) drop the body without calling the callback again, every other
  redirect with a streaming body is `WTLS_ERR_HTTP_REDIRECT` (a stream
  cannot be rewound). Pinned by the `http` suite on host and Wii
  (fixed-length and chunked uploads, 256 KiB, aborts, keep-alive reuse,
  redirect rules).
- Client certificates / mutual TLS (`wtls_client_cred_load`,
  `wtls_client_cred_free`, `wtls_options::client_cred`): ECDSA and RSA key
  pairs in PEM or DER, cert/key match validated at load. Reference-counted
  per connection (including pooled HTTP connections), private key zeroized
  on the last release. Session tickets and the HTTP keep-alive pool are
  keyed by the leaf certificate's SHA-256 fingerprint (identities never mix),
  and a redirect that leaves the origin strips the client certificate like
  `Authorization` and `Cookie`. The parsing lives in its own object file
  (`src/wtls_cred.c`); applications without mTLS link none of it (new
  `cred_parse_not_linked_without_mtls` CTest).
- HTTP helpers (`wtls_http_url_encode`, `wtls_http_parse_content_range`,
  `<wiitls/http.h>`): RFC 3986 percent-encoding with a size-query mode and
  an RFC 9110 Content-Range parser for resumable `206` downloads
  (unsatisfied ranges, `start > end`, `end >= total` and overflow rejected).
  Stateless in their own object file; host unit tests in
  `tests/host/test_http_util.c`.
- Test PKI (`tests/host/make_certs.py`): client certificates (ECDSA P-256,
  RSA-2048, an untrusted one) with `clientAuth`; the test server gained
  mutual-TLS ports (certificate required and optional, the latter answers
  `/who` with the client CN) and `CLIENTCERT`/`CLIENTKEY` plain-port commands,
  so the Wii hardware suites load credentials over the network without file
  access.

### Fixed

- Wii `wtls_plat_send` (`src/plat/plat_ogc.c`): IOS `/dev/net/ip/top` queues a
  partial prefix into the TCP stream while still returning `-EAGAIN` (`-11`)
  when a non-blocking `net_send` exceeds the remaining ~4080-byte socket send
  buffer, causing retries to duplicate bytes and corrupt TLS records during
  sustained uploads (such as `http_upload_256k`). `wtls_plat_send` now checks
  `IOS_POLLOUT` with a zero timeout (returning `WTLS_WANT_WRITE` when full)
  and temporarily clears `IOS_O_NONBLOCK` around `net_send`. Pinned on host
  and Wii by `test_send_truthfulness` (`plat`), `spike` `[8]`–`[11]` and
  `http_upload_256k` (`http`).

## 0.2.0 (2026-11-25)

### Added

- HTTP/1.1 client, `<wiitls/http.h>`, with its own parser (RFC 9112 framing,
  no third-party HTTP code):
  - `wtls_http_open` / `wtls_http_read` / `wtls_http_close` stream a
    response; `wtls_http_fetch` reads a whole body into memory (1 MiB limit
    by default).
  - `https://` over libwiitls TLS and plain `http://`.
  - Content-Length, chunked and close-delimited bodies; trailers skipped;
    1xx interim responses skipped.
  - gzip Content-Encoding: offered by default and decoded while reading
    (zlib 1.3.1 inflate, compiled in; build option `WTLS_GZIP`, default ON).
  - Keep-alive pool (4 idle connections, 60 s) with a liveness check before
    reuse and one retry of idempotent requests on a stale connection.
  - Redirects (301, 302, 303, 307, 308; 5 by default); `Authorization`,
    `Proxy-Authorization` and `Cookie` are dropped when a redirect leaves
    the origin; `https` → `http` is refused.
  - `wtls_http_get_header`, `wtls_http_header_at`, `wtls_http_get_info`
    (effective URL, redirects, reuse, TLS facts), `wtls_http_pool_flush`.
- `WTLS_VERSION` macro in `<wiitls/wiitls.h>`.
- Error codes `WTLS_ERR_HTTP`, `WTLS_ERR_HTTP_LIMIT`, `WTLS_ERR_HTTP_REDIRECT`.
- Tests: host suites for URL, request, head parser, framing, chunked and gzip
  decoding, and an HTTP integration suite against a local test server; an
  ASan/UBSan CI job; the Wii runner gets an `http` suite, real-world HTTP
  tests and HTTP benchmarks.

### Changed

- The `https_get` example uses the HTTP client instead of raw TLS and
  hand-parsed headers; it accepts `http://` URLs and shows connection reuse.
- `wtls_shutdown` also closes the HTTP keep-alive pool.
- The release package contains `include/wiitls/http.h` and the zlib licence.

## 0.1.0 (2026-11-24)

First release: TLS 1.3 / 1.2 client for Wii homebrew on Mbed TLS 4.1.1 and
libogc, with certificate verification against the embedded Mozilla CA
bundle, session tickets, a DNS cache, blocking and non-blocking I/O, SNTP
clock sync and an IOS-jitter entropy source.
