# Game-Booter: Herkunft

`boot/` ist der Game-Booter aus **WiiFlow Lite**. Er läuft nach dem Launcher ab `0x80A80000`, liest die Startparameter
(`the_CFG`) und die Fragmentliste. Dann setzt er die Disc-Emulation des cIOS auf, führt den Apploader des Spiels aus,
wendet die Standard-Patches an und springt ins Spiel.

| | |
|---|---|
| Repository | https://github.com/Fledge68/WiiFlow_Lite |
| Pfad | `resources/wiiflow_game_booter/` |
| Commit | `69c0d4d61984f4ed6fca51b4520320cadfac960d` (2026-08-19, `master`) |
| Autoren | FIX94, Fledge68 und die WiiFlow-Mitwirkenden. Einzelne Dateien von Nuke, WiiPower, The Lemon Man, Nicksasa, Hector Martin, Andre Heider, Dimok, blackb0x, crediar u. a. (siehe Dateiköpfe) |
| Lizenz | GPLv3 oder später (WiiFlow Lite), einzelne Dateien GPLv2 bzw. GPLv2 oder später; siehe „Lizenzen“ |

## Dateien

Übernommen: alle Dateien aus `source/`, unverändert bis auf die Liste unten.

| Datei | Inhalt |
|---|---|
| `main.cpp` | Ablauf: CFG lesen, Disc öffnen, Apploader, Patches, Sprung |
| `apploader.c/h`, `disc.c/h`, `wdvd.c/h`, `fst.c/h` | Apploader, Disc-Zugriff über `/dev/di` des cIOS, Fragmentliste an das cIOS |
| `patchcode.c/h`, `videopatch.c/h`, `kirbypatch.c/h`, `patchhook.S`, `codehandler*.h`, `multidol.c/h`, `wip.c/h` | Spiel-Patches (Video, Sprache, Hooks; Cheats und WIP werden vom Launcher nicht genutzt) |
| `cios.c/h` | IOS-Informationen (`CurrentIOS` kommt aus `the_CFG`) |
| `ChannelHandler.cpp/hpp`, `lz77.c/h`, `fs.c/h` | Kanalstart (`TYPE_CHANNEL`). Nicht genutzt, aber `main.cpp` verzweigt dorthin; Entfernen hätte `main.cpp` stärker verändert, als das Behalten kostet. |
| `video_tinyload.c/h`, `gecko.c/h`, `memory.h`, `utils.h`, `types.h`, `frag.h`, `Config.hpp` | Hilfen und Typen |

Weggelassen: nur `Makefile` (ersetzt durch das CMake-Ziel `nl_game_booter` in `CMakeLists.txt`).

Neu in newlauncher:

- `include/booter_cfg.h`: Übergabeformat, gemeinsam für Launcher und Booter (siehe unten)
- `include/booter_defines.h`: Ersatz für WiiFlows `source/defines.h`
- `nl/exception_close.cpp`: Ersatz für `__exception_closeall()` aus altem libogc

## Änderungen

| Datei | Änderung | Grund |
|---|---|---|
| `main.cpp:58` | `memcpy(&normalCFG, (void*)NL_WII_CFG_ADDR, …)` statt `memcpy(&normalCFG, (void*)*EXT_ADDR_CFG, …)` | Der Launcher legt `the_CFG` fest bei `0x93200000` ab. `0x90100000` liegt im MEM2-Heap des Launchers; ein Schreiben dort vor dem Abbau könnte Malloc-Metadaten zerstören (Plan M9). |
| `Config.hpp`, `types.h`, `frag.h`, `cios.h`, `wip.h` | Die Definitionen von `the_CFG`, den `TYPE_*`/`IOS_TYPE_*`-Enums, `MAX_FRAG`/`Fragment`/`FragList`, `IOS_Info` und `WIP_Code` stehen wortgleich in `include/booter_cfg.h`; die Header binden sie ein. | Eine Definition für Launcher und Booter. Ein `static_assert` hält das Layout fest (121 Byte bzw. 240 012 Byte). |
| `patchcode.c:31` | `#include "booter_defines.h"` statt `#include "../../../source/defines.h"` | Der Booter liegt nicht mehr im WiiFlow-Baum. Gebraucht werden nur `APP_NAME`/`APP_VERSION` für die Patcher-Kennung an Wiimmfi; der Launcher meldet sich ehrlich als `newlauncher <Version>`. |
| alle | Zeilenenden CRLF → LF | `.gitattributes` des Repos (`eol=lf`) |

### Bauoptionen (CMake-Ziel `nl_game_booter`)

| Option | Grund |
|---|---|
| `-O1 -Wall -Wextra -DHAVE_CONFIG_H` | wie upstream-Makefile |
| `-Wno-shift-negative-value -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-overread` | wie upstream-Makefile. Die drei letzten sind dort als `FALSE_POSITIVES` markiert: GCC meldet sie bei festen Speicheradressen (`(u32*)0x80000000` u. ä.). |
| `-std=gnu17` (nur C) | GCC 15+ übersetzt C standardmäßig als C23; dort heißt `int (**main)()` „keine Argumente“, und `apploader.c` passt nicht mehr zum Zeigertyp. Upstream wurde mit gnu17 gebaut; Code bleibt unverändert. |
| `-Wa,-mno-regnames` (nur `patchhook.S`) | Die Plattform setzt `-mregnames` für alle Assemblerdateien. `patchhook.S` definiert `r0`…`r31` selbst per `.set`; mit `-mregnames` meldet gas „invalid register expression“. |
| `-Wl,--section-start,.init=0x80A80000` | Ladeadresse wie upstream |

### libogc

Gebaut gegen das installierte libogc (devkitPro, tuxedo-basiert). Einzige API-Lücke: `__exception_closeall()` gibt es nicht mehr.
`nl/exception_close.cpp` bildet sie nach wie im alten libogc:

- `MSR_EE` aus
- `MSR_FP|MSR_RI` an
- jeden der 15 Ausnahmevektoren (`0x100` … `0x1700`) mit `rfi` überschreiben, Cache leeren

Das Spiel setzt beim `OSInit` eigene Vektoren. Ob das neue `crt0` beim Start des Booters etwas tut, das das alte nicht tat,
lässt sich nur auf der Hardware prüfen (Abnahme W1–W9).

## Übergabeformat (`include/booter_cfg.h`)

| Konstante | Wert | Herkunft |
|---|---|---|
| `NL_WII_CFG_ADDR` | `0x93200000` | Plan M9 (statt Zeiger bei `0x90100000`, WiiFlow `source/booter/external_booter.cpp`) |
| `NL_FRAG_LIST_ADDR` | `0x93201000` | Plan M9 |
| `WBFS_DEVICE_USB` / `WBFS_DEVICE_SDHC` | 1 / 2 | `source/libwbfs/libwbfs.h:12–13`, gesetzt in `source/loader/wbfs.c:216`; der Booter prüft `wbfsDev == 2` (`main.cpp:111`) |
| `kNlVidModeSystem` | 1 | `disc.c:164` `Disc_SelectVMode`, `case 1: // SYSTEM` |
| `kNlConfigByteUnpatched` | `0xCD` | `source/menu/menu_game_boot.cpp:35` (`setLanguage`): Sprache nicht patchen |
| `kNlAspectRatioGame` | −1 | `menu_game_boot.cpp:1114`: Einstellung 0 („Spielstandard“) minus 1 |
| `kNlDeflickerGame` | 0 | `menu_game_boot.cpp:1121–1122` |
| `kNlVideoWidthGame` | 0 | `menu_game_boot.cpp:1124–1125` |

**`JustDanceGame`**: wahr, wenn der Spieltitel „Just Dance“ enthält (`menu_game_boot.cpp:1099–1101`, `wcsstr(hdr->title, L"Just Dance")`).

**`size` der Fragmentliste**: `source/loader/frag.c:64` (`frag_append`) setzt `ff->size = offset + count` bei jedem Anhängen.
Die Größe ist also das Ende des zuletzt angehängten Fragments, in 512-Byte-Sektoren.

- WBFS: Fragmente kommen in Disc-Reihenfolge, also Ende des letzten Fragments.
- ISO: WiiFlow hängt die Cluster der Datei über `_FAT_get_fragments` (eigener libfat-Fork) an. Die Länge ergibt sich aus der Dateigröße, also `ceil(Dateigröße / 512)`.

Geprüft wird die Größe im Booter nur in `frag_get` (`offset + count > size` → Fehler, `frag.c:120`).

## Lizenzen

- GPLv3 oder später: `Config.hpp`, `main.cpp`
- GPLv2 oder später: `fst.c/h`, `patchcode.c/h`
- zlib-artig („Altered source versions must be plainly marked“): `ChannelHandler.cpp` (dude, 2010), unverändert
- **GPLv2 ohne „oder später“**:
  - `video_tinyload.c/h`: „GNU GPL, version 2“, aus HBC-Stub/TinyLoad
  - `lz77.c/h`: „GNU General Public License (v2)“
- ohne Kopf: übrige Dateien (Teil von WiiFlow Lite, GPLv3)

**Offen:** GPLv2-only und GPLv3 sind streng genommen nicht kombinierbar. WiiFlow Lite verteilt dieselbe Kombination seit Jahren.
Vor einer öffentlichen Veröffentlichung des Booters muss das geklärt sein. Mögliche Wege:

- `lz77` entfällt mit dem Kanalstart
- `video_tinyload` (≈ 100 Zeilen Videomodus-Setup) wird ersetzt
