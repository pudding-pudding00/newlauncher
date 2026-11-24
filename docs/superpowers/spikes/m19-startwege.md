# Spike M19: GC-Disc über Nintendont und d2x-NAND-Emulation

**Stand:** 2026-11-24

## 1. GameCube-Disc über Nintendont

**Frage:** Wie löst man in Nintendont den Start einer eingelegten GameCube-Disc über `NIN_CFG` aus, und auf welchen Plattformen wird das unterstützt?

**Quelle:** `FIX94/Nintendont`, Commit `7a183459939653cb7bcf59ab137c2758a9f9fae0`. Gelesen wurden `common/include/CommonConfig.h`, `loader/source/main.c` und `loader/source/menu.c`. Kein Code übernommen (der Nintendont-Kernel steht unter GPLv2-only).

### Ergebnis: positiv

- **Pseudo-Eintrag im Menü:** `loader/source/menu.c:325-335` erzeugt bei `!IsWiiU()` den Eintrag `ID = "DISC01"`, `Name = "Boot GC Disc in Drive"`, `Path = "di:di"`. Auf Wii U (vWii) entfällt der Eintrag außer im speziellen Wii-VC-Modus (`isWiiVC`, `menu.c:315-324`).
- **Eintrag in `NIN_CFG`:** `loader/source/menu.c:1873-1880` schneidet `"di:"` ab (`Path + 3`, dann `if (StartChar[0] == ':') StartChar++;`), sodass `ncfg->GamePath` exakt `"di"` enthält.
- **Konfigurationsbits (`common/include/CommonConfig.h`):**
  - `NIN_CFG_AUTO_BOOT` (`1u << 7`): `loader/source/main.c:841-855` bootet ohne Menü direkt mit `ncfg->GamePath`.
  - `NIN_CFG_USB` (`1u << 10`): `loader/source/main.c:832` setzt `UseSD = (ncfg->Config & NIN_CFG_USB) == 0;`. Bei Disc-Start darf `NIN_CFG_USB` **nicht** gesetzt sein, damit `/saves` (Memory-Card-Emulation) und Cheat-Pfade auf SD gesucht werden.
  - Alle Spieloptionen aus M14 (Videomodus, Sprache, Memory Card, `max_pads`, `skip_ipl`, `cheats`, `cheatPath`, …) bleiben wirksam.
- **Ladeweg im Loader (`loader/source/main.c:859-927`):**
  - Bei `memcmp(ncfg->GamePath, "di", 3) == 0` (und `!isWiiVC`) initialisiert Nintendont das Laufwerk über `DI_Init()` / `DI_Mount()` / `DI_Close()`, liest über `ReadRealDisc` (`DIP_CMD_NORMAL`, Rückfall `DIP_CMD_DVDR`) den Disc-Header, prüft `IsGCGame(DIBuf)` und kopiert die 4-Byte-GameID nach `ncfg->GameID`.
- **Plattformgrenzen:** Nur auf der Wii mit GameCube-Ports (`SystemInfo.platform != Platform::VWii && gcPorts`), nie auf vWii (kein GC-Laufwerk) und nie auf Konsolen ohne GC-Ports (RVL-101 / Wii mini nehmen physisch keine 8-cm-Discs).

### Entscheidung für Task 4

- `kNinDiscGamePath = "di"`, ohne `kNinCfgUsb`, `kNinCfgAutoBoot` gesetzt.
- `preflightGame` prüft auf Wii + `gcPorts`, verweigert sonst mit `LaunchFailure::GcDiscUnsupported`.

---

## 2. d2x-NAND-Emulation (Teil- und Vollmodus)

**Frage:** Wie schaltet man die d2x-NAND-Emulation ein und aus, wann muss sie gegenüber `IOS_ReloadIOS` und Booter aktiviert werden, welche Partitionen unterstützt d2x und welche Lizenz gilt je Datei?

**Quellen:**
- `Fledge68/WiiFlow_Lite`, Commit `69c0d4d61984f4ed6fca51b4520320cadfac960d`:
  - `source/channel/nand.cpp:1-28, 63-199, 1239-1249` und `source/channel/nand.hpp`
  - `source/channel/channel_launcher.cpp:20-115` (`Identify`)
  - `source/menu/menu_game_boot.cpp:980-1016, 1280-1301`
- Gegenprüfung im cIOS selbst: `wiidev/d2x-cios`, Commit `33ad1eeeb8f562df99e7d7ca428fdd36e0e31be7`:
  - `source/fat-module/main.c:284-331`, `source/fat-module/fat_wrapper.c:42-44, 98-189`, `source/fat-module/ff.c:2045-2053`
  - `source/ffs-plugin/ioctl.h:40-41`, `source/ffs-plugin/plugin.h:28-31`, `source/ffs-plugin/plugin.c:144-214, 762-775, 932-951`, `source/ffs-plugin/fs_tools.c:44-113`
  - `source/mload-module/patches.c:46-84, 306-345`

### Ergebnis: positiv

- **Ioctl-Aufrufe an `fat` und `/dev/fs` (d2x-cIOS):**
  1. **FAT-Gerät einhängen (`IOS_Open("fat", 0)`):**
     - `IOS_Ioctlv` mit Befehl `0xF0` (SD) bzw. `0xF2` (USB), 1 Eingabevektor: `s32 partition` (`0..3` für die 0-basierte primäre MBR-Partition bzw. Superfloppy, `-1` für erste FAT-Partition).
     - `d2x-cios/source/fat-module/fat_wrapper.c:153-161` und `ff.c:2048-2052` prüfen explizit nur die 4 MBR-Primärpartitionen (`0..3`) über FatFs `LD2PT`. Logische EBR-Partitionen, GPT, NTFS, ext und WBFS werden von d2x nicht gemountet.
     - Aushängen über `IOS_Ioctlv(fd, 0xF1 /* SD */ / 0xF3 /* USB */, 0, 0, NULL)`.
  2. **NAND-Umleitung einschalten (`IOS_Open("/dev/fs", 0)`):**
     - `IOS_Ioctlv(fd, 100 /* IOCTL_ISFS_SETMODE */, 2, 0, vector)`:
       - `vector[0]`: `u32 mode` — `0x01` (`MODE_SDHC`) oder `0x02` (`MODE_USB`), optional verodert mit `0x100` (`MODE_FULL`). (`MODE_REV17 = 0x80` setzt das d2x-`ffs-plugin` wegen `#define FORCE_MODE_REV17` in `plugin.c:36, 156-158` selbst.)
       - `vector[1]`: `char* nandPath` inklusive NUL-Byte, z. B. `"/nands/emu1"` (`FAT_MAXPATH = 256` im cIOS; `parseEmuNandPath` im Launcher begrenzt auf ≤ 64 Zeichen ohne Ende-Slash).
     - **Abschalten:** `u32 zero = 0; IOS_Ioctl(fd, 100, &zero, sizeof(zero), NULL, 0);` und `IOS_Ioctlv` `0xF1`/`0xF3` an `"fat"`.
- **Teil- vs. Vollmodus (`d2x-cios/source/ffs-plugin/fs_tools.c:44-113`):**
  - **Partial (`FullMode = 0`, für Wii-Spiele mit `save = emu`):** leitet `/title/00010000`, `00010001`, `00010004`, `00010005`, `/ticket/00010001`, `00010005`, `/tmp` (ohne `/tmp/uid.sys`), `/import`, `/sys/disc.sys` um; Systemdateien wie `SYSCONF` und `/sys/cert.sys` bleiben vom echten NAND. Passt exakt zu „EmuNAND nur nutzen“ ohne Titel-/Systemkopie.
  - **Full (`FullMode = 0x100`, für Kanäle `TYPE_CHANNEL`):** leitet jeden `/`-Pfad außer `/dev` und `/tmp/launch.sys` auf den EmuNAND um.
- **Timing & Zustand über den Booter-Sprung:**
  - `boot/source/main.cpp` lädt **kein** IOS neu (es nutzt das bereits vom Launcher per `IOS_ReloadIOS(iosSlot)` geladene cIOS weiter).
  - `nl::launch::chainload` ruft `SYS_ResetSystem(SYS_SHUTDOWN, 0, 0)` auf; das schließt nur PPC-Offene Handles, verändert aber den Zustand der laufenden Starlet-Module (`fat-module` / `ffs-plugin`) im aktiven d2x-cIOS **nicht**.
  - **Ort der Einschaltung:** Im Launcher in `nl::launch::bootWiiGame()`, unmittelbar **nach** `IOS_ReloadIOS(iosSlot)` und **vor** `chainload(booter)`. Zuvor sind im Launcher alle PPC-Dateisysteme per `unmountAll()` ausgehängt, sodass keine parallelen Zugriffe auf SD/USB stattfinden.
  - **Vorteil für `booter_cfg.h`:** `struct the_CFG` bleibt unverändert bei **121 Byte**, der vorhandene `TYPE_CHANNEL`-Zweig in `boot/source/main.cpp:118-126` greift ohne Booter-Umbau.
  - **Kanalstart (`TYPE_CHANNEL`):** Vor `chainload` wird nach `enableNandEmu` (im Full-Modus) `ES_Identify` (`identifyEmuChannel`) mit `/title/<hi>/<lo>/content/title.tmd`, gefälschtem RSA-Ticket und `/sys/cert.sys` aufgerufen (`WiiFlow_Lite/source/channel/channel_launcher.cpp:20-115`). Fehlen Zertifikate im EmuNAND, dient `/sys/cert.sys` vom echten NAND (vor dem IOS-Wechsel gelesen) als Rückfall. `d2x-cios/source/mload-module/patches.c:46-84` patcht die `ES_Identify`- und ISFS-Berechtigungsprüfungen im cIOS direkt, sodass `PatchIOS` aus `identify.c` unter d2x nicht nötig ist.
  - **Schutz gegen Hängen:** Schlägt `enableNandEmu` fehl, wird sofort `disableNandEmu()` gerufen und per `SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0)` abgebrochen. Der Launcher selbst läuft nie mit aktiver Emulation.
  - **Upstream-Bug vermieden:** `WiiFlow_Lite/source/channel/nand.cpp:131` deklariert `static u32 mode = Device->Mode | FullMode;` mit Einmal-Initialisierung. Unser Port weist `mode` vor jedem `IOS_Ioctlv` neu zu und flusht alle DMA-Puffer mit `DCFlushRange`.

### Übernommene Dateien und Lizenzen (Spec 3.3)

| Datei | Vorlage | Lizenz | Verträglich mit GPLv3 |
|---|---|---|---|
| `src/platform/nand_emu.cpp` (Mount/Enable/Disable) | `WiiFlow_Lite/source/channel/nand.cpp` (Miigotu 2011, OverjoY 2012) | **Zlib** („Altered source versions must be plainly marked“) | ja (Eintrag in `THIRD_PARTY.md`, Änderung im Kopf vermerkt) |
| `src/platform/nand_emu.cpp` (`identifyEmuChannel`) | `WiiFlow_Lite/source/channel/channel_launcher.cpp` | **GPL-3.0-or-later** | ja |
| `boot/source/wdvd.c` (zusätzlich im Launcher-Target) | Bereits in `boot/` (M9/M11), WiiFlow Lite | **GPL-3.0-or-later** | ja (bereits in `THIRD_PARTY.md`) |

Nintendont-Code (`FIX94/Nintendont`) wird nicht übernommen.
