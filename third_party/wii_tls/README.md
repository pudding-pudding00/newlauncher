# libwiitls

Modern TLS for Wii homebrew: a small C library that gives homebrew apps
HTTPS-grade connections to today's Internet: **TLS 1.3** (TLS 1.2 fallback),
certificate verification against the embedded Mozilla CA bundle, session
resumption and DNS caching for low latency.

Built on [Mbed TLS 4.1.1](https://github.com/Mbed-TLS/mbedtls) (with
TF-PSA-Crypto) and libogc.

- TLS 1.3 preferred, TLS 1.2 minimum; ChaCha20-Poly1305 first (fast without AES
  hardware), AES-GCM as fallback; X25519, P-256, P-384.
- Certificate chain + host name are always verified (incl. IP literals via
  iPAddress SAN). The 118 Mozilla roots live in `.rodata` and are parsed lazily,
  only the one needed by a handshake.
- TLS 1.3 session tickets (8-entry cache) and a 16-entry DNS cache: a second
  connection to the same host skips DNS and the certificate-chain work.
- Blocking API with timeouts, plus a non-blocking mode for your own poll loop.
- Entropy from the timing jitter of IOS IPC round trips, measured on a Wii
  ([docs/ios-socket-behaviour.md](docs/ios-socket-behaviour.md); health-tested,
  SHA-256 conditioned, fails closed if the tests fail; `wtls_init` spends
  about 400 ms seeding); SNTP clock sync for the often-wrong Wii RTC.

> **Validated on a Wii.** The hardware suites pass on a real Wii over Wi-Fi:
> `plat`, `crypto`, `time`, `tls` and `realworld`, the last against public HTTPS
> servers. With the default build, a full TLS 1.3 handshake costs about 66 ms of
> CPU (81 ms wall) and a resumed one about 14 ms. ChaCha20-Poly1305 runs at
> 24 MB/s, and an X25519 key exchange (keygen, export, agreement) takes 4.6 ms.
> The full matrix and the targets that are still missed are in
> [docs/benchmarks.md](docs/benchmarks.md).

**Documentation:** [usage guide](docs/usage.md) (setup, threading, error
handling, resumption, non-blocking I/O, limits) ·
[performance](docs/performance.md) · API reference in
[include/wiitls/wiitls.h](include/wiitls/wiitls.h). Prebuilt packages for
devkitPPC are on the GitHub releases page.

## Quick example

```c
#include <wiitls/wiitls.h>

/* on your network thread, after net_init() */
wtls_init();
static const char *const alpn[] = {"http/1.1", NULL};
wtls_options opt = {0};
opt.alpn = alpn;

wtls_conn *c;
int r = wtls_connect(&c, "example.com", 443, &opt);
if (r == WTLS_ERR_CLOCK) {                 /* Wii clock wrong: sync and retry */
    wtls_time_sync_sntp(NULL, 3000);
    r = wtls_connect(&c, "example.com", 443, &opt);
}
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
thread, second request showing `resumed=yes`) is in
[examples/https_get](examples/https_get/main.c):

```bash
wiiload build-wii/examples/https_get/https_get.dol url=https://example.com/
```

## Building

Requirements: CMake ≥ 3.20, a network connection on the first configure (Mbed
TLS is fetched via `FetchContent`).

**Wii** (devkitPro with devkitPPC and libogc):

```bash
cmake -S . -B build-wii -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake
cmake --build build-wii -j
```

Produces `build-wii/libwiitls.a`, the hardware test runner
`build-wii/tests/hw/wtls_hwtest.dol` and the example
`build-wii/examples/https_get/https_get.dol`. Options: `-DWTLS_BUILD_HWTEST=OFF`,
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

Suites: `selftest`, `spike`, `entropy`, `plat`, `time`, `tls` (against the PC
test server), `realworld` (example.com, google.com, cloudflare.com, badssl.com).

## Using it in your project

**Prebuilt release package:** see [docs/usage.md](docs/usage.md#1-add-it-to-your-project).
`tools/make_release.sh` (in a devkitPro shell) builds the package into
`dist/` and checks that the example links against it.

**As a subdirectory** (recommended: Mbed TLS and the include path come along):

```cmake
add_subdirectory(third_party/libwiitls)
target_link_libraries(my_app PRIVATE wiitls)
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
- Mozilla CA certificate bundle (`data/cacert.pem`, `src/gen/ca_bundle.c`):
  [MPL-2.0](https://www.mozilla.org/MPL/2.0/).

Applications that ship libwiitls must carry the Apache-2.0 notice of Mbed TLS
and the MPL-2.0 notice for the CA data.
