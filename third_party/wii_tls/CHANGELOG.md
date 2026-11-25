# Changelog

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
