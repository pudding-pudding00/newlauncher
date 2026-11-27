# Third-party components

newlauncher is licensed under the **GNU General Public License v3 or later** (`LICENSE`). The decision and its
reasons are in `docs/superpowers/specs/2026-11-11-v2-universell-design.md`, section 3.

This table lists every component contained in the release (`boot.dol`, game booter). The license texts are in
`LICENSES/`; the release ZIP appends them to `THIRD_PARTY.txt`. `tools/check_licenses.py` checks in CI that every
linked library and every folder under `third_party/` is listed here. The "About" page in the settings shows
name, version and license from this table (generated at build time).

| Name | Version | License | Source | Distributed as |
|---|---|---|---|---|
| WiiFlow Lite (Game-Booter) | see `boot/UPSTREAM.md` | GPL-3.0-or-later / GPL-2.0-or-later | https://github.com/Fledge68/WiiFlow_Lite | source in `boot/`, linked into `game_booter.dol` |
| WiiFlow Lite (NAND-Emulation) | 69c0d4d (`source/channel/nand.cpp`) | Zlib, GPL-3.0-or-later | https://github.com/Fledge68/WiiFlow_Lite | ported to `src/platform/nand_emu.cpp`; `LICENSES/Zlib.txt` |
| Gecko-Codehandler (Dolphin `docs/codehandler.s`) | da9e932 | MIT | https://github.com/dolphin-emu/dolphin (© 2010–2017 Nuke, brkirch, Y.S, Kenobi, gamemasterplc) | source in `boot/nl/codehandler/`, assembled during the build, in `game_booter.dol`; `LICENSES/MIT.txt` |
| wii_tls | libwiitls 1.0.0 (14058b6, Release v1.0.0) | Apache-2.0 | https://github.com/pudding-pudding00/libwiitls, vendored under `third_party/wii_tls` (commit in `VENDORED.txt`) | statically linked, only with `NL_TLS=ON`; `LICENSES/Apache-2.0.txt` |
| Mbed TLS | 4.1.2 LTS | Apache-2.0 | https://github.com/Mbed-TLS/mbedtls (release archive, SHA256 in `third_party/wii_tls/CMakeLists.txt`) | statically linked through wii_tls, only with `NL_TLS=ON`; `LICENSES/Apache-2.0.txt` |
| Mozilla CA certificates | 2026-11-05 | MPL-2.0 | https://curl.se/docs/caextract.html | embedded unchanged through wii_tls, only with `NL_TLS=ON`; `LICENSES/MPL-2.0.txt` |
| M PLUS Rounded 1c | 1.059 | OFL-1.1 | https://github.com/google/fonts/tree/main/ofl/mplusrounded1c | embedded Latin-1 subset; `LICENSES/OFL-1.1.txt` |
| libogc (with libasnd, libbte, libwiiuse) | 3.2.1 | Zlib, BSD-2-Clause | https://github.com/devkitPro/libogc | statically linked; `LICENSES/libogc.txt` |
| libfat | 2.1.0 | ZPL-2.1 | https://github.com/devkitPro/libfat | statically linked; `LICENSES/ZPL-2.1.txt` |
| libntfs | ae00fc8 (with 4 patches, see `third_party/libntfs/VENDORED.txt`) | GPL-2.0-or-later (used under GPLv3 here); `cache2.c`, `bit_ops.h`: BSD-3-Clause | https://github.com/extremscorner/libntfs | vendored under `third_party/libntfs`, statically linked, only with `NL_FS_NTFS=ON`, read-only; GPLv3 text in `LICENSE`, upstream GPLv2 text in `third_party/libntfs/upstream/LICENSE`, `LICENSES/BSD-3-Clause.txt` |
| FreeType | 2.14.1 | FTL | https://freetype.org | statically linked; `LICENSES/FTL.txt` (Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.) |
| libpng | 1.6.55 | libpng-2.0 | https://www.libpng.org/pub/png/libpng.html | statically linked; `LICENSES/libpng.txt` |
| zlib | 1.3.1 | Zlib | https://zlib.net | statically linked; `LICENSES/Zlib.txt`; with `NL_TLS=ON` additionally inflate (`Z_PREFIX`) through wii_tls |
| bzip2 | 1.0.8 | bzip2-1.0.6 | https://sourceware.org/bzip2/ | statically linked (FreeType); `LICENSES/bzip2.txt` |
| Brotli | 1.2.0 | MIT | https://github.com/google/brotli | statically linked (FreeType); `LICENSES/MIT.txt` |
| libogg | 1.3.6 | BSD-3-Clause | https://xiph.org/ogg/ | statically linked; `LICENSES/BSD-3-Clause.txt` |
| Tremor (libvorbisidec) | 1.2.1 | BSD-3-Clause | https://gitlab.xiph.org/xiph/tremor | statically linked; `LICENSES/BSD-3-Clause.txt` |

## Not distributed

- **doctest** (MIT, `third_party/doctest`): only for host tests, not part of the release.
- Users download covers, metadata, cheats, emulators and Nintendont themselves; they are not part of the release.

## New dependencies

Allowed are GPLv2-or-later, GPLv3, LGPL 2.1+/3, Apache-2.0, MPL-2.0, MIT, BSD, zlib, public domain, CC0 and OFL.
Rejected are GPLv2-only, LGPLv2-only without an upgrade clause, "non-commercial only", unclear origin and binary
blobs without source. Every new dependency gets a row in this table **before** it is added.
