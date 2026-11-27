# newlauncher

[![CI](https://github.com/pudding-pudding00/newlauncher/actions/workflows/build.yml/badge.svg)](https://github.com/pudding-pudding00/newlauncher/actions/workflows/build.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

**A fast, Wii game launcher for homebrew and cIOS setups.** One library for Wii, GameCube, N64 and
retro consoles, sorted by console generation.

## What it offers

- **One library for everything:** Wii games natively (built-in game booter), GameCube through Nintendont,
  N64 through Virtual Console injects, Not64 or Wii64, and NES, SNES, GB, GBC and GBA through emulators.
  Optical discs, EmuNAND and installed channels are supported too.
- **Covers and banners:** downloads Wii/GameCube covers and retro box art, shows static GameCube banners and
  animated channel banners with audio.
- **Finding games:** favorites, recently played, A–Z jump bar and live search with an on-screen keyboard.
- **Per-game settings and cheats:** start method, video, language and Gecko codes.
- **Emulator management:** installs and updates emulators from the Open Shop Channel.
- **Saves:** local backup and restore, and cloud sync with Google Drive, OneDrive or Dropbox
  ([setup guide](docs/cloud-setup.md)).
- **Comfort:** light and dark theme, background music, German and English UI, self-update.
- **Modern HTTPS** (TLS 1.3, HTTP/2) through [libwiitls](https://github.com/pudding-pudding00/libwiitls).

Hardware acceptance for the newer features is still in progress; see [docs/STATUS.md](docs/STATUS.md).

## Requirements

- A Wii (or vWii on Wii U) with the Homebrew Channel
- An SD card; a USB drive for larger libraries
- A d2x cIOS to start Wii games from USB or SD
- [Nintendont](https://github.com/FIX94/Nintendont) 6.493 or newer for GameCube games

## Installing

Every merge into `main` publishes a development build on the
[Releases page](https://github.com/pudding-pudding00/newlauncher/releases) (`dev-0.0.N`), and the newest one is what the launcher's self-update offers. Unzip its ZIP to the SD
card, or load its `.dol` with wiiload. To build it yourself and copy it to the SD card (`E:` stands for the card's
drive):

```bash
python tools/build_wii.py
python tools/wii_deploy.py E:
```

The deploy tool refuses a card without an `apps/` folder unless you add `--init`. Then start **newlauncher**
from the Homebrew Channel. The [user guide](docs/user-guide.md) explains the folder layout, controls and
settings.

## Building

You need devkitPro with Wii development support, CMake 3.25 or newer, Ninja and Python 3.

```bash
python tools/build_wii.py        # Wii build with TLS, output in build/wii/
cmake --preset host && cmake --build --preset host && ctest --preset host   # host unit tests
```

Details, Dolphin smoke tests and hardware testing: [docs/development.md](docs/development.md).

## Documentation

| Document | Content |
|---|---|
| [User guide](docs/user-guide.md) | SD layout, covers, emulators, N64, GameCube, controls, settings |
| [Cloud saves](docs/cloud-setup.md) | Registering your own app with Google, Microsoft or Dropbox |
| [Development](docs/development.md) | Building, testing, deploying, log markers |
| [Status](docs/STATUS.md) | Milestones and open items |
| [Hardware acceptance](docs/hardware-acceptance.md) | Checklist for a real Wii |
| [Third-party components](THIRD_PARTY.md) | Libraries, versions and licenses |

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md). Please report security problems privately, as described in
[SECURITY.md](SECURITY.md).

This project ships no games, ROMs or keys. Use your own backups.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Third-party components keep their own licenses
([THIRD_PARTY.md](THIRD_PARTY.md)). The embedded font is M PLUS Rounded 1c (SIL Open Font License 1.1,
see `assets/fonts/OFL.txt`).

Not affiliated with or endorsed by Nintendo. Wii and GameCube are trademarks of Nintendo.
