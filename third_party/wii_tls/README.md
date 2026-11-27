# libwiitls

[![CI](https://github.com/pudding-pudding00/libwiitls/actions/workflows/ci.yml/badge.svg)](https://github.com/pudding-pudding00/libwiitls/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/pudding-pudding00/libwiitls)](https://github.com/pudding-pudding00/libwiitls/releases/latest)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

**Modern TLS 1.3 and HTTPS for Nintendo Wii homebrew.** A small C library for
devkitPPC / libogc that lets Wii applications talk securely to today's
Internet — APIs, CDNs, updaters, leaderboards, cloud saves, or your own
home server — without fighting outdated crypto or IOS socket quirks.

Built on [Mbed TLS 4.1 LTS](https://github.com/Mbed-TLS/mbedtls) (with
TF-PSA-Crypto), [zlib](https://zlib.net/), and libogc.

## What it offers

- **Modern TLS:** TLS 1.3 preferred, TLS 1.2 minimum. ChaCha20-Poly1305 first
  (fast on PowerPC without AES hardware), AES-GCM as fallback; X25519,
  P-256 and P-384 key exchange; ALPN support.
- **Always-on certificate verification:** verifies the certificate chain,
  expiry, and host name (including IPv4 literals via `iPAddress` SAN)
  against the embedded Mozilla CA bundle (118 roots in `.rodata`, parsed
  lazily).
- **Custom trust, pinning & mutual TLS:**
  - Add or replace trusted CA roots at runtime (`wtls_add_trusted_ca()`,
    `wtls_reset_trusted_cas()`), or omit the ~200 KiB embedded bundle with
    `-DWTLS_EMBED_CA=OFF`.
  - Pin server public keys (up to 2 SHA-256 SPKI pins: primary + backup).
  - Present client certificates (`wtls_client_cred_load()`,
    `wtls_client_cred_load_ex()` for password-protected keys, ECDSA or RSA in
    PEM/DER) with reference-counted lifetime and identity isolation.
- **Fast reconnects:** TLS 1.3 session ticket cache (8 entries) and
  multi-IPv4 DNS cache (16 entries, connect fallback across up to 4
  A-records) — a second connection skips DNS and certificate-chain work.
- **Full HTTP/2 and HTTP/1.1 client (`<wiitls/http.h>`):**
  - Automatic HTTP/2 (`h2` ALPN, RFC 9113 + HPACK RFC 7541, sequential stream
    reuse, bidirectional flow control; opt out via `req.no_h2` or CMake
    `-DWTLS_HTTP2=OFF`) with seamless fallback to HTTP/1.1.
  - `https://` and `http://`, whole-body `wtls_http_fetch()` or streaming
    `wtls_http_open()` / `wtls_http_read()` for large files, plus
    `wtls_http_detach()` for `101 Switching Protocols` upgrades (e.g.
    WebSockets).
  - Streaming uploads (`wtls_http_body_cb`) with `Content-Length`,
    `Transfer-Encoding: chunked`, and `Expect: 100-continue`.
  - Built-in `gzip` and `deflate` decompression (zlib inflate), keep-alive
    connection pool (4 idle connections, automatic retry of idempotent
    requests on stale sockets), `http://` proxy & `CONNECT` tunneling, and
    redirect handling (strips `Authorization`, `Proxy-Authorization`, `Cookie`
    and client certs across origins; refuses `https` → `http` downgrades).
  - URL/form encoding (`wtls_http_url_encode()`, `wtls_http_form_encode()`),
    streaming `multipart/form-data` (`wtls_http_multipart_*`), and `Range` /
    `Content-Range` helpers (`wtls_http_format_range()`,
    `wtls_http_parse_content_range()`, `wtls_http_check_resume()`).
  - Download progress callback (`wtls_http_progress_cb`) in both
    `wtls_http_fetch()` and `wtls_http_open()`/`wtls_http_read()`.
- **Built for the Wii:**
  - Blocking API with per-stage timeouts, non-blocking connect/handshake
    (`wtls_connect_start()`, `wtls_connect_step()`), and non-blocking I/O
    (`wtls_set_nonblocking()`) for your own `net_poll()` loop.
  - Cooperative cancellation (`abort_cb`): cancel connects, handshakes,
    downloads, or uploads within ≤ 100 ms (e.g. when the user presses
    **B** or **HOME**).
  - Entropy collected from IOS IPC timing jitter, health-tested
    (SP 800-90B RCT/APT) and SHA-256 conditioned — fails closed if health
    tests fail.
  - Built-in SNTP clock sync (`wtls_time_sync_sntp()`) for consoles with an
    unset or drifted RTC.
  - Automatic detection of Wii IP changes (`net_gethostip()`), plus
    `wtls_net_reset()` and workarounds for IOS socket bugs
    ([docs/ios-socket-behaviour.md](docs/ios-socket-behaviour.md)).
- **Tuned for the Broadway CPU:** 32-bit-limb X25519 (constant-time) and
  P-256 ECDSA verification. On a real Wii over Wi-Fi: ~27 ms CPU for a full
  TLS 1.3 handshake, ~16 ms CPU for a resumed one, ~3 ms CPU for a pooled
  HTTP request, and 24 MB/s ChaCha20-Poly1305 decryption (far above Wii
  Wi-Fi throughput). Details in [docs/performance.md](docs/performance.md).

## Quick start

Download `libwiitls-<version>-wii.zip` from the
[releases page](https://github.com/pudding-pudding00/libwiitls/releases/latest),
unpack it, and add it to your devkitPro Makefile:

```make
WIITLS   := path/to/libwiitls-<version>-wii
CFLAGS   += -I$(WIITLS)/include
LIBPATHS += -L$(WIITLS)/lib
LIBS     := -lwiitls -lmbedtls -lmbedx509 -ltfpsacrypto -lwiitls -lwiiuse -lbte -logc -lm
```

(`-lwiitls` appears twice because TF-PSA-Crypto calls back into it.)

### HTTPS request

```c
#include <wiitls/http.h>

/* on your network thread, after net_init() */
wtls_init();

wtls_http_request req = {0};
req.url = "https://example.com/";
wtls_http_result res;
int r = wtls_http_fetch(&req, 0, &res);   /* whole body, up to 1 MiB */
if (r == WTLS_ERR_CLOCK) {                /* Wii clock wrong: sync and retry */
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

### Raw TLS

For protocols other than HTTP, use the TLS connection directly:

```c
#include <wiitls/wiitls.h>

static const char *const alpn[] = {"http/1.1", NULL};
wtls_options opt = {0};
opt.alpn = alpn;

wtls_conn *c;
if (wtls_connect(&c, "example.com", 443, &opt) == WTLS_OK) {
    const char req[] = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    wtls_write(c, req, sizeof req - 1);
    char buf[2048];
    int n;
    while ((n = wtls_read(c, buf, sizeof buf)) > 0) fwrite(buf, 1, n, stdout);
    /* n == WTLS_ERR_CLOSED: clean end of stream */
    wtls_close(c);
}
```

Complete Wii programs are in [examples/](examples/):
[https_get](examples/https_get/main.c) (UI thread + network thread,
connection reuse) and [upload_mtls_abort](examples/upload_mtls_abort/main.c)
(streaming upload, mutual TLS, SPKI pinning, progress, cancel with the
Wiimote). The release package includes both as ready-to-run `.dol` files:

```bash
wiiload https_get.dol url=https://example.com/
```

## Documentation

- **[Usage guide](docs/usage.md)** — setup, the threading rule, the clock,
  HTTP, resumption, non-blocking I/O, client certificates, pinning, error
  handling, limits, and versioning.
- **API reference** — the headers
  [include/wiitls/wiitls.h](include/wiitls/wiitls.h) and
  [include/wiitls/http.h](include/wiitls/http.h).
- [Performance](docs/performance.md) and
  [benchmark methodology](docs/benchmarks.md).
- [IOS socket and entropy measurements](docs/ios-socket-behaviour.md).
- [Security review](docs/security-review.md) and
  [security policy](SECURITY.md).
- [Changelog](CHANGELOG.md).

## Things to know

- **One thread.** libwiitls has no internal locks: call every `wtls_*`
  function from the same thread, typically a dedicated network LWP thread so
  the UI keeps drawing while a handshake blocks. Debug builds check this.
- **The clock matters.** Certificates need a correct date and the Wii's clock
  is often wrong. Sync with `wtls_time_sync_sntp()` at start-up, or on
  `WTLS_ERR_CLOCK` / an expired-certificate error.
- **`wtls_init()` takes about 400 ms** to seed the random generator.
- **Client only.** No server mode, no DTLS, no HTTP/3, no certificate
  revocation checks (OCSP/CRL).

## Using it from CMake

```cmake
# As a subdirectory (fetches Mbed TLS and zlib on the first configure):
add_subdirectory(third_party/libwiitls)
target_link_libraries(my_app PRIVATE wiitls::wiitls)

# Or from the release package / an installed prefix:
find_package(wiitls 1.0 REQUIRED)   # with -DCMAKE_PREFIX_PATH=<package dir>
target_link_libraries(my_app PRIVATE wiitls::wiitls)
```

## Building from source

Requirements: CMake ≥ 3.20, Ninja (recommended), and a network connection on
the first configure (Mbed TLS and zlib are fetched with `FetchContent`,
hash-checked).

### Wii (Linux, macOS, Windows)

Requires [devkitPro](https://devkitpro.org/) with `devkitPPC` and `libogc`.

**Linux / macOS / Windows (MSYS2 devkitPro shell):**

```bash
cmake -S . -B build-wii -G Ninja -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake"
cmake --build build-wii
```

**Windows (PowerShell / cmd, without MSYS2 shell):**

```powershell
cmake -S . -B build-wii-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wii-win-toolchain.cmake
cmake --build build-wii-win
```

This builds `libwiitls.a`, the examples, the hardware test runner
`tests/hw/wtls_hwtest.dol` and the benchmark `bench/wtls_bench.dol`
(`-DWTLS_BUILD_EXAMPLES=OFF`, `-DWTLS_BUILD_HWTEST=OFF`,
`-DWTLS_BUILD_BENCH=OFF` to skip them). `tools/make_release.sh` builds the
release package into `dist/` and checks that it links.

### Host (Linux, macOS, Windows)

For development and running the test suite on your PC (needs Python 3,
`openssl`, and GCC or Clang; on Windows use MSYS2 UCRT64 or MinGW-w64):

```bash
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

**On a Wii** (PC and Wii on the same network, Homebrew Channel open):

```bash
python tools/hwtest.py --wii <wii-ip> --dol build-wii/tests/hw/wtls_hwtest.dol --suite all
```

Suites: `plat`, `crypto`, `time`, `tls`, `http`, `soak` (against a test
server on the PC), `realworld` (public HTTPS servers and CDNs), plus the
diagnostic `selftest`, `spike`, `entropy` and `jitterdump`.

## Tested

Every release passes the host suites (Linux, Windows, with ASan/UBSan and
libFuzzer) in CI, and the full hardware matrix on a retail Wii over Wi-Fi:
`plat` 13/13, `crypto` 14/14, `tls` 40/40, `http` 86/86, `time` 1/1,
`realworld` 15/15, `soak` 128 iterations with no heap growth, bench 10/10.

The entropy source has so far been characterised on one console. Reports
from other Wii revisions, the vWii and USB Ethernet adapters are very welcome
— see [CONTRIBUTING.md](CONTRIBUTING.md#hardware-reports).

## Updating the CA bundle

The library embeds the Mozilla CA bundle from `data/cacert.pem`
([curl's extract](https://curl.se/docs/caextract.html)); a monthly CI job
checks for updates. To update by hand:

```bash
curl -o data/cacert.pem https://curl.se/ca/cacert.pem
curl -o data/cacert.pem.sha256 https://curl.se/ca/cacert.pem.sha256
python tools/gen_ca_bundle.py --pem data/cacert.pem --sha256 data/cacert.pem.sha256 --out src/gen/ca_bundle.c
```

When Python 3 is found, the build regenerates the bundle itself (and
verifies the SHA-256); otherwise it uses the checked-in `src/gen/ca_bundle.c`,
so commit the regenerated file too. Applications can also add roots at
runtime with `wtls_add_trusted_ca()`.

## Contributing and security

Bug reports, hardware reports and pull requests are welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md). Please report vulnerabilities privately
as described in [SECURITY.md](SECURITY.md).

## License

libwiitls is licensed under the [Apache License 2.0](LICENSE),
Copyright 2026 pudding-pudding00. It bundles:

- [Mbed TLS / TF-PSA-Crypto](https://github.com/Mbed-TLS/mbedtls):
  Apache-2.0 (statically linked).
- [zlib](https://zlib.net/) 1.4 inflate (with `WTLS_GZIP`, the default):
  zlib license (compiled into `libwiitls.a`).
- The Mozilla CA certificate bundle (`data/cacert.pem`,
  `src/gen/ca_bundle.c`): [MPL-2.0](https://www.mozilla.org/MPL/2.0/).

Applications that ship libwiitls must carry the Apache-2.0 notice of Mbed TLS
and the MPL-2.0 notice for the CA data; the release package has all licence
texts in `licenses/`.
