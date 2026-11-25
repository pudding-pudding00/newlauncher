# libwiitls

Modern TLS for Wii homebrew: a small C library that gives homebrew apps
HTTPS-grade connections to today's Internet: **TLS 1.3** (TLS 1.2 fallback),
certificate verification against the embedded Mozilla CA bundle, session
resumption and DNS caching for low latency, and an HTTP/1.1 client on top.

Built on [Mbed TLS 4.1.1](https://github.com/Mbed-TLS/mbedtls) (with
TF-PSA-Crypto) and libogc.

- TLS 1.3 preferred, TLS 1.2 minimum; ChaCha20-Poly1305 first (fast without AES
  hardware), AES-GCM as fallback; X25519, P-256, P-384.
- Certificate chain + host name are always verified (incl. IP literals via
  iPAddress SAN); optional mutual TLS (`wtls_client_cred_load`) with ECDSA
  and RSA client certificates. The 118 Mozilla roots live in `.rodata` and are
  parsed lazily, only the one needed by a handshake.
- TLS 1.3 session tickets (8-entry cache) and a 16-entry DNS cache: a second
  connection to the same host skips DNS and the certificate-chain work.
- Blocking API with timeouts, plus a non-blocking mode for your own poll loop.
- HTTP/1.1 client (`<wiitls/http.h>`, own parser): `https://` and `http://`,
  streaming downloads/uploads (`wtls_http_body_cb`, fixed or chunked) or
  whole-body `wtls_http_fetch`, gzip decompression (zlib inflate, built in),
  keep-alive pool with reuse, redirects with credential stripping across
  origins, plus URL encoding and `Content-Range` helpers.
- Entropy from the timing jitter of IOS IPC round trips, measured on a Wii
  ([docs/ios-socket-behaviour.md](docs/ios-socket-behaviour.md); health-tested,
  SHA-256 conditioned, fails closed if the tests fail; `wtls_init` spends
  about 400 ms seeding); SNTP clock sync for the often-wrong Wii RTC.

> **Validated on a Wii.** All hardware suites pass on a real Wii over Wi-Fi
> (`suite=all`: `time` 1/1, `plat` 13/13, `crypto` 14/14, `tls` 40/40, `http` 86/86,
> `soak` 128 iterations with 0 heap growth, and `realworld` 15/15 across public
> HTTPS/CDN servers — **170/170 pass**; `wtls_bench.dol` **10/10 pass**). With the default build (`WTLS_ACCEL_P256=ON`), a full
> TLS 1.3 handshake costs about 27 ms of CPU (37 ms wall — meeting the ≤ 30 ms
> CPU target), a resumed one about 16 ms (25 ms wall), and a further HTTP
> request over a kept-alive connection about 3.0 ms CPU (6.6 ms wall).
> ChaCha20-Poly1305 runs at 24 MB/s, an X25519 key exchange takes 4.6 ms, and
> a P-256 ECDSA verify takes 5.3–5.4 ms. The full matrix and the resumed-handshake
> target are in [docs/benchmarks.md](docs/benchmarks.md).

**Documentation:** [usage guide](docs/usage.md) (setup, threading, error
handling, resumption, non-blocking I/O, limits) ·
[performance](docs/performance.md) · API reference in
[include/wiitls/wiitls.h](include/wiitls/wiitls.h) and
[include/wiitls/http.h](include/wiitls/http.h) · [changelog](CHANGELOG.md). Prebuilt packages for
devkitPPC are on the GitHub releases page.

## Quick example

```c
#include <wiitls/http.h>

/* on your network thread, after net_init() */
wtls_init();
wtls_http_request req = {0};
req.url = "https://example.com/";
wtls_http_result res;
int r = wtls_http_fetch(&req, 0, &res);    /* whole body, up to 1 MiB */
if (r == WTLS_ERR_CLOCK) {                 /* Wii clock wrong: sync and retry */
    wtls_time_sync_sntp(NULL, 3000);
    r = wtls_http_fetch(&req, 0, &res);
}
if (r == WTLS_OK) {
    printf("%d, %u bytes\n%s\n", res.status, (unsigned)res.body_len, res.body);
    wtls_http_result_free(&res);
} else {
    printf("%s\n", wtls_strerror(r));
}
```

Streaming, keep-alive, redirects and gzip are in the
[usage guide](docs/usage.md#5-http-requests).

### Raw TLS

For protocols other than HTTP, use the TLS connection directly:

```c
#include <wiitls/wiitls.h>

static const char *const alpn[] = {"http/1.1", NULL};
wtls_options opt = {0};
opt.alpn = alpn;

wtls_conn *c;
int r = wtls_connect(&c, "example.com", 443, &opt);
if (r == WTLS_OK) {
    const char req[] = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    wtls_write(c, req, sizeof req - 1);
    char buf[2048];
    int n;
    while ((n = wtls_read(c, buf, sizeof buf)) > 0) fwrite(buf, 1, n, stdout);
    /* n == WTLS_ERR_CLOSED: clean end of stream */
    wtls_close(c);
} else {
    printf("%s\n", wtls_strerror(r));
}
```

A complete Wii program (spinner on the UI thread, all networking on an LWP
thread, second request showing `reused=yes`) is in
[examples/https_get](examples/https_get/main.c), and a streaming upload +
mTLS/SPKI + Wiimote cancel (`abort_cb`) example is in
[examples/upload_mtls_abort](examples/upload_mtls_abort/main.c):

```bash
wiiload build-wii/examples/https_get/https_get.dol url=https://example.com/
wiiload build-wii/examples/upload_mtls_abort/upload_mtls_abort.dol url=https://httpbin.org/post
```

## Building

Requirements: CMake ≥ 3.20, a network connection on the first configure (Mbed
TLS is fetched via `FetchContent`).

**Wii** (devkitPro with devkitPPC and libogc):

```bash
cmake -S . -B build-wii -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake
cmake --build build-wii -j
```

**Wii natively on Windows** (devkitPro + Ninja, without MSYS2 shell):

```powershell
cmake -S . -B build-wii-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wii-win-toolchain.cmake
cmake --build build-wii-win
```

Produces `libwiitls.a`, the hardware test runner
`tests/hw/wtls_hwtest.dol`, `bench/wtls_bench.dol`, and the examples in
`examples/`. Options: `-DWTLS_BUILD_HWTEST=OFF`, `-DWTLS_BUILD_BENCH=OFF`,
`-DWTLS_BUILD_EXAMPLES=OFF`.

**Host** (Linux/macOS/Windows with gcc or clang; for development and tests;
needs Python 3 and `openssl` for the test server and certificates):

```bash
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

**Hardware tests** (PC and Wii on the same network, Homebrew Channel open):

```bash
python tools/hwtest.py --wii 192.168.1.50 --dol build-wii/tests/hw/wtls_hwtest.dol --suite tls
```

Suites: `selftest`, `spike`, `entropy`, `plat`, `crypto`, `time`, `tls`, `http`,
`soak` (against the PC test server; pass `--extra iters=1000` for endurance runs),
`realworld` (example.com, google.com, cloudflare.com, badssl.com, Fastly,
Let's Encrypt, nginx, Caddy, GitHub CDN Range).

## Using it in your project

**Prebuilt release package:** see [docs/usage.md](docs/usage.md#1-add-it-to-your-project).
`tools/make_release.sh` (in a devkitPro shell) builds the package into
`dist/` and checks both Makefile and `find_package(wiitls)` consumer builds.

**As a subdirectory or installed package** (recommended: Mbed TLS and the include path come along):

```cmake
add_subdirectory(third_party/libwiitls)
target_link_libraries(my_app PRIVATE wiitls::wiitls)

# Or after `cmake --install <build> --prefix <prefix>`:
find_package(wiitls 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE wiitls::wiitls)
```

**Prebuilt archives:** add `include/` to the include path and link, in this
order, `libwiitls.a libmbedtls.a libmbedx509.a libtfpsacrypto.a libwiitls.a`
(the archives are in `build-wii/` and `build-wii/_deps/mbedtls-build/`;
libwiitls appears twice because TF-PSA-Crypto calls back into it for entropy),
then libogc's `-lwiiuse -lbte -logc -lm` as usual.

## Thread model

libwiitls has no internal locks. **Call every `wtls_*` function from one single
thread**: typically a dedicated network LWP thread, so the UI thread keeps
drawing while a handshake or `wtls_read` blocks. The rule covers all functions,
because the CA store, ticket cache, DNS cache and crypto state are global.
Hand data between your threads with your own queue/mutex.

Session tickets (TLS 1.3 resumption) arrive after the handshake and are
captured only while you call `wtls_read`/`wtls_write` on the connection; a
connection closed right after `wtls_connect` stores no ticket.

For an event loop, `wtls_set_nonblocking(c, true)` makes `wtls_read`/`wtls_write`
return `WTLS_WANT_READ`/`WTLS_WANT_WRITE` instead of waiting; poll
`wtls_get_fd(c)`, but check `wtls_pending(c)` first: decrypted bytes can be
buffered while the socket is not readable.

## Error codes

| Code | Value | Meaning |
|------|------:|---------|
| `WTLS_OK` | 0 | success |
| `WTLS_WANT_READ` / `WTLS_WANT_WRITE` | -1 / -2 | non-blocking only: retry when the fd is ready (same buffer for writes) |
| `WTLS_ERR_ARG` | -10 | invalid argument |
| `WTLS_ERR_STATE` | -11 | `wtls_init` not called |
| `WTLS_ERR_NOMEM` | -12 | out of memory |
| `WTLS_ERR_CANCELLED` | -13 | cancelled via `abort_cb` |
| `WTLS_ERR_ENTROPY` | -20 | random generator could not be seeded |
| `WTLS_ERR_CLOCK` | -21 | clock implausible: call `wtls_time_sync_sntp` and retry |
| `WTLS_ERR_NET` | -30 | network not initialised (`net_init`) |
| `WTLS_ERR_DNS` | -31 | name not resolved |
| `WTLS_ERR_CONNECT` | -32 | TCP connect failed |
| `WTLS_ERR_TIMEOUT` | -33 | connect, handshake or blocking I/O timed out |
| `WTLS_ERR_IO` | -34 | TCP reset, or stream ended without close_notify (truncation) |
| `WTLS_ERR_CLOSED` | -35 | peer closed cleanly (close_notify): normal end of stream |
| `WTLS_ERR_HANDSHAKE` | -40 | TLS handshake failed |
| `WTLS_ERR_CERT` | -41 | certificate rejected; `wtls_last_verify_flags()` has the `MBEDTLS_X509_BADCERT_*` bits. `EXPIRED`/`FUTURE` on an unsynced Wii usually means a wrong clock: sync with SNTP and retry once |
| `WTLS_ERR_HTTP` | -50 | malformed or unsupported HTTP response, corrupt gzip body |
| `WTLS_ERR_HTTP_LIMIT` | -51 | response head, field count or `wtls_http_fetch` body over its limit |
| `WTLS_ERR_HTTP_REDIRECT` | -52 | too many redirects, bad `Location`, or redirect `https` → `http` |
| `WTLS_ERR_INTERNAL` | -99 | unexpected library error |

`wtls_strerror(code)` gives a short text. The full API documentation is in
[include/wiitls/wiitls.h](include/wiitls/wiitls.h).

## Trusted CAs

The library embeds the Mozilla CA bundle from `data/cacert.pem`
([curl's extract](https://curl.se/docs/caextract.html)). Additional roots
(e.g. for your own server) can be added at runtime with
`wtls_add_trusted_ca(der_or_pem, len)` after `wtls_init`.

**Updating the bundle:**

```bash
curl -o data/cacert.pem https://curl.se/ca/cacert.pem
curl -o data/cacert.pem.sha256 https://curl.se/ca/cacert.pem.sha256
python tools/gen_ca_bundle.py --pem data/cacert.pem --sha256 data/cacert.pem.sha256 --out src/gen/ca_bundle.c
```

When Python 3 is found, the build regenerates the bundle from `data/cacert.pem`
itself (and verifies the SHA-256); otherwise it uses the checked-in
`src/gen/ca_bundle.c`, so commit the regenerated file too.

## Licenses

- libwiitls itself: [Apache-2.0](LICENSE), Copyright 2026 pudding-pudding00.
- [Mbed TLS / TF-PSA-Crypto](https://github.com/Mbed-TLS/mbedtls): Apache-2.0
  (fetched at build time, statically linked).
- [zlib](https://zlib.net/) 1.3.1 inflate (with `WTLS_GZIP`, the default):
  zlib license (fetched at build time, compiled into `libwiitls.a`).
- Mozilla CA certificate bundle (`data/cacert.pem`, `src/gen/ca_bundle.c`):
  [MPL-2.0](https://www.mozilla.org/MPL/2.0/).

Applications that ship libwiitls must carry the Apache-2.0 notice of Mbed TLS
and the MPL-2.0 notice for the CA data.
