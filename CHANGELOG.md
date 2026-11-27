# Changelog

All notable changes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

First public state of the project. No stable release has been published yet; see [docs/STATUS.md](docs/STATUS.md) for
the completed milestones.

### Added

- Development releases: every merge into `main` publishes a release `dev-0.0.N` (numbered from 0.0.1) with the Wii
  ZIP for the SD card, the DOL for wiiload, `update.ini` for the launcher's self-update and `SHA256SUMS`. The newest one is the latest release.

### Fixed

- The focus ring follows the cover's shape instead of surrounding the whole tile.
