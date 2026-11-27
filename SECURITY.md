# Security Policy

## Supported versions

Only the latest release and `main` receive fixes.

## Reporting a vulnerability

Please do **not** open a public issue. Report privately through
[GitHub Security Advisories](https://github.com/pudding-pudding00/newlauncher/security/advisories/new) ("Report a vulnerability" in the **Security** tab).
Include the affected component, steps to reproduce and the target (Wii, vWii, Dolphin or PC tools).

We aim to acknowledge reports within 7 days.

## Scope notes

- **HTTPS** uses [libwiitls](https://github.com/pudding-pudding00/libwiitls) with certificate verification always
  on. Vulnerabilities in the library are best reported in its repository.
- **Cloud tokens** on the SD card are only obfuscated with a key derived from the console. Whoever holds both the SD
  card and the console can use them; see [docs/cloud-setup.md](docs/cloud-setup.md).
- **LAN features** (remote control on TCP port 4300, Dropbox pairing page on HTTP port 4301) are meant for a
  trusted home network only.
- **Self-update** downloads the latest published release of this repository over HTTPS.
