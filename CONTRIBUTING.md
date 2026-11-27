# Contributing to newlauncher

Bug reports, hardware reports, documentation fixes and pull requests are all welcome.

## Reporting

- Bugs: open an issue with the **Bug report** template.
- Results from a real console: use the **Hardware report** template with
  [docs/hardware-acceptance.md](docs/hardware-acceptance.md).
- Security problems: **no** public issue, see [SECURITY.md](SECURITY.md).

## Development setup

See [docs/development.md](docs/development.md).

## Before opening a pull request

- Work on a feature branch, never directly on `main`.
- Write a failing test first, then the code.
- Host and tool tests pass; the Wii builds with and without TLS compile with 0 warnings.
- `src/core/` stays free of `<ogc/...>` so it can be tested on a PC. No exceptions, no RTTI.
- Commits, pull requests, documentation and new comments are in English. UI strings exist in German **and**
  English (`src/ui/strings.hpp`).
- Use conventional commit prefixes (`feat:`, `fix:`, `docs:`, `chore:`).
- Never commit game files (ROMs, ISOs, WADs), third-party banners, keys, tokens or personal data (IP or MAC
  addresses, local paths, e-mail addresses).
- `third_party/wii_tls` is only updated with `python tools/vendor_wii_tls.py <checkout> --rev <tag>`. Certificate
  verification is never disabled and HTTPS never falls back to HTTP.

## Releasing (maintainers)

1. Set the version in `CMakeLists.txt` (`project(newlauncher VERSION ...)`) and `dist/apps/newlauncher/meta.xml`.
2. Move the `[Unreleased]` entries in `CHANGELOG.md` into a new version section.
3. Run the hardware acceptance on a Wii.
4. Commit, tag `vX.Y.Z` and push the tag. The `release` workflow builds the package and creates a **draft**
   release with the ZIP, `update.ini` and `SHA256SUMS`.
5. Review the draft and publish it. Installed launchers pick up the latest published release through
   self-update.

## License

By contributing you agree that your contributions are licensed under the
[GNU GPL v3 or later](LICENSE), the license of this project.
