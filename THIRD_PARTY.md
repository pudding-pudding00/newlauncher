# Drittanbieter-Komponenten

newlauncher steht unter der **GNU General Public License v3 oder später** (`LICENSE`). Die Entscheidung
und ihre Gründe stehen in `docs/superpowers/specs/2026-11-11-v2-universell-design.md`, Abschnitt 3.

Diese Tabelle listet jede Komponente, die im Release (`boot.dol`, Game-Booter) steckt. Die Lizenztexte
liegen in `LICENSES/`; das Release-ZIP hängt sie an `THIRD_PARTY.txt` an. `tools/check_licenses.py`
prüft in der CI, dass jede gelinkte Bibliothek und jeder Ordner unter `third_party/` hier steht.
Die Einstellungsseite „Über“ zeigt Name, Version und Lizenz aus dieser Tabelle (zur Build-Zeit erzeugt).

| Name | Version | Lizenz | Quelle | verteilt als |
|---|---|---|---|---|
| WiiFlow Lite (Game-Booter) | Stand siehe `boot/UPSTREAM.md` | GPL-3.0-or-later / GPL-2.0-or-later | https://github.com/Fledge68/WiiFlow_Lite | Quelltext in `boot/`, gelinkt in `game_booter.dol` |
| wii_tls | siehe `third_party/wii_tls/VENDORED.txt` | Apache-2.0 | Projekt des Autors, vendort unter `third_party/wii_tls` | statisch gelinkt; `LICENSES/Apache-2.0.txt` |
| Mbed TLS | 4.1.1 | Apache-2.0 | https://github.com/Mbed-TLS/mbedtls | statisch gelinkt über wii_tls; `LICENSES/Apache-2.0.txt` |
| Mozilla CA-Zertifikate | Stand siehe `third_party/wii_tls/data/cacert.pem` | MPL-2.0 | https://curl.se/docs/caextract.html | unverändert eingebettet über wii_tls; `LICENSES/MPL-2.0.txt` |
| M PLUS Rounded 1c | 1.059 | OFL-1.1 | https://github.com/google/fonts/tree/main/ofl/mplusrounded1c | Latin-1-Teilmenge eingebettet; `LICENSES/OFL-1.1.txt` |
| libogc (mit libasnd, libbte, libwiiuse) | 3.2.1 | Zlib, BSD-2-Clause | https://github.com/devkitPro/libogc | statisch gelinkt; `LICENSES/libogc.txt` |
| libfat | 2.1.0 | ZPL-2.1 | https://github.com/devkitPro/libfat | statisch gelinkt; `LICENSES/ZPL-2.1.txt` |
| FreeType | 2.14.1 | FTL | https://freetype.org | statisch gelinkt; `LICENSES/FTL.txt` (Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.) |
| libpng | 1.6.55 | libpng-2.0 | https://www.libpng.org/pub/png/libpng.html | statisch gelinkt; `LICENSES/libpng.txt` |
| zlib | 1.3.1 | Zlib | https://zlib.net | statisch gelinkt; `LICENSES/Zlib.txt` |
| bzip2 | 1.0.8 | bzip2-1.0.6 | https://sourceware.org/bzip2/ | statisch gelinkt (FreeType); `LICENSES/bzip2.txt` |
| Brotli | 1.2.0 | MIT | https://github.com/google/brotli | statisch gelinkt (FreeType); `LICENSES/MIT.txt` |
| libogg | 1.3.6 | BSD-3-Clause | https://xiph.org/ogg/ | statisch gelinkt; `LICENSES/BSD-3-Clause.txt` |
| Tremor (libvorbisidec) | 1.2.1 | BSD-3-Clause | https://gitlab.xiph.org/xiph/tremor | statisch gelinkt; `LICENSES/BSD-3-Clause.txt` |

## Nicht verteilt

- **doctest** (MIT, `third_party/doctest`): nur für Host-Tests, landet nicht im Release.
- Cover, Metadaten, Cheats, Emulatoren und Nintendont lädt der Nutzer selbst; sie sind nicht Teil des Releases.

## Neue Abhängigkeiten

Erlaubt sind GPLv2-oder-später, GPLv3, LGPL 2.1+/3, Apache-2.0, MPL-2.0, MIT, BSD, zlib, Public Domain, CC0 und
OFL. Abgelehnt sind GPLv2-only, LGPLv2-only ohne Umstiegsklausel, „nur nicht-kommerziell“, unklare Herkunft und
Binär-Blobs ohne Quelle. Jede neue Abhängigkeit bekommt **vor** dem Einbau eine Zeile in dieser Tabelle.
