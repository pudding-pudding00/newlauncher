# Changelog

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
