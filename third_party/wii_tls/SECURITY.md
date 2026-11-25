# Security Policy

## Supported Versions

| Version | Status |
|---|---|
| `1.0.x` | Active maintenance (SemVer 1.x production line) |
| `< 1.0.0` | End of life — please upgrade |

## Reporting a Vulnerability

Please do **not** open a public GitHub issue for security vulnerabilities.
Report vulnerabilities privately via GitHub Security Advisories
("Report a vulnerability" in the repository's **Security** tab) or contact the
maintainers directly through their GitHub profile contact information.

Please include:
- A description of the issue and affected component/API.
- Steps to reproduce or a minimal proof-of-concept input (e.g. a libFuzzer
  reproducer or malformed certificate/HTTP response).
- Which build target is affected (Wii / devkitPPC, host, or both).

We aim to acknowledge reports within 7 days and coordinate a fix and advisory
before public disclosure.

## Security Model & Scope Notes

- **Single-thread contract:** `libwiitls` has no internal locks. Calling
  `wtls_*` from multiple threads without external synchronisation is unsupported
  (caught at runtime in Debug / `-DWTLS_THREAD_CHECK=ON` builds).
- **Certificate verification & pinning:** All TLS connections verify the
  certificate chain and hostname (or IP SAN) by default unless
  `insecure_skip_verify` is set (tests only). OCSP/CRL revocation is out of
  scope; use `spki_pins` (SHA-256 SPKI pinning) and regular CA bundle updates
  where stricter server binding is required.
- **Entropy on Wii:** Randomness is collected from IOS IPC timing jitter and
  continuously health-tested (SP 800-90B RCT/APT). `wtls_init()` fails closed
  with `WTLS_ERR_ENTROPY` if health tests fail.
- **Non-goals:** Server mode, DTLS, HTTP/2, HTTP/3, and password-protected
  private keys are outside the library scope.

## Maintenance Lifecycle (CA Bundle & Mbed TLS)

- **Mozilla CA Bundle (`data/cacert.pem`, `src/gen/ca_bundle.c`):** Checked
  monthly by `.github/workflows/maintenance.yml` against `curl.se/ca/cacert.pem`
  and its published `.sha256`. Updates regenerate `src/gen/ca_bundle.c` via
  `tools/gen_ca_bundle.py`.
- **Mbed TLS 4.1.x LTS:** Patch releases on the `4.1.x` LTS branch are tracked
  monthly; tarball `SHA256` and all configure-time patch anchors in
  `CMakeLists.txt` are verified at every bump.
