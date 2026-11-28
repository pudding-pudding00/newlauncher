# Contributing to libwiitls

Thanks for helping! Bug reports, hardware reports, documentation fixes and
pull requests are all welcome.

## Reporting bugs

Open an issue with the **Bug report** template. Most useful are:

- the libwiitls version (`wtls_version()`) and how you built it (release
  package or from source, devkitPPC/libogc versions);
- the failing call, its return value and `wtls_strerror()` text, and for
  `WTLS_ERR_CERT` the value of `wtls_last_verify_flags()`;
- the server (host name, or what software it runs) if the problem is
  server-specific.

Security problems: please **don't** open a public issue, see
[SECURITY.md](SECURITY.md).

## Hardware reports

The IOS-jitter entropy source and the socket workarounds have been measured
on one retail Wii over Wi-Fi. Results from other console revisions, the vWii
on a Wii U, or USB Ethernet adapters are very valuable:

```sh
python tools/hwtest.py --wii <wii-ip> --dol build-wii/tests/hw/wtls_hwtest.dol --suite all
python tools/hwtest.py --wii <wii-ip> --dol build-wii/tests/hw/wtls_hwtest.dol --suite jitterdump --timeout 300 > jd.log
python tools/jitter_assess.py jd.log
```

Open an issue with the **Hardware report** template and paste the summary
lines (`=== suite … ===`, `WTLS-HWTEST DONE …`) and the `jitter_assess.py`
output. Background: [docs/ios-socket-behaviour.md](docs/ios-socket-behaviour.md).

## Development setup

You need CMake ≥ 3.20, Ninja (recommended), Python 3 and `openssl` for the host
build, and devkitPro with devkitPPC and libogc for the Wii build. See
[Building from source](README.md#building-from-source).

```bash
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Before opening a pull request, please make sure that:

- the host tests pass, ideally also with sanitizers:
  `-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`;
- the Wii build still compiles (`-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake`);
- changes to networking, crypto or the platform layer have been run on a Wii
  (`tools/hwtest.py`, at least the affected suites) — say in the PR if you
  could not;
- changes to parsers keep the fuzz harnesses building
  (`-DCMAKE_C_COMPILER=clang -DWTLS_BUILD_FUZZ=ON`).

CI runs all of this except the hardware tests on every push.

## Code guidelines

- C11, compiled with `-Wall -Wextra`; no new warnings.
- Follow the style of the surrounding code: 4-space indentation, `wtls_`
  prefix for public symbols, `snake_case`, comments that explain *why*.
- Public API changes must be backwards compatible within 1.x: no removed or
  changed functions, error codes or struct fields; new struct fields go at
  the end and must default to the old behaviour when zero.
- Secrets are wiped with `mbedtls_platform_zeroize()`. Code that handles
  secret data must stay constant-time (`tools/check_ct.cmake` checks the
  X25519 code).
- Generated files (`src/gen/ca_bundle.c`, `src/accel/*.inc`, `tests/common/wycheproof_*.inc`) are regenerated
  with the scripts in `tools/`, never edited by hand.
- Add tests for bug fixes and new features: `tests/common/` is shared by the
  host and Wii runners.
- Add a line to the `[Unreleased]` section of [CHANGELOG.md](CHANGELOG.md)
  for anything a user would notice, and update [docs/usage.md](docs/usage.md)
  and the header comments when behaviour changes.

## Releasing (maintainers)

When bumping the Mbed TLS LTS release, check the configure-time patches first
([docs/mbedtls-patches.md](docs/mbedtls-patches.md)); how the release is built, signed and
verified is described in [docs/releasing.md](docs/releasing.md).

1. Update the version in `CMakeLists.txt` (`project(libwiitls VERSION …)`) and
   `include/wiitls/wiitls.h` (`WTLS_VERSION` and `WTLS_VERSION_*`; the
   configure step checks that they match).
2. Move the `[Unreleased]` entries in `CHANGELOG.md` to a new version section
   and update the compare links at the bottom.
3. Run the hardware matrix (`--suite all`, `wtls_bench.dol`) on a Wii. Do
   this and check the changelog section **before** tagging: pushing the tag
   publishes the release.
4. Commit, tag `vX.Y.Z` and push the tag. The `release` workflow runs all CI
   jobs on the tag, builds the package with `tools/make_release.sh`, and
   publishes the GitHub release with the zip, `SHA256SUMS` and the changelog
   section as notes. If any job fails, nothing is published.
5. Add highlights and the hardware results to the published release notes.

## License

By contributing you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE), the license of this project.
