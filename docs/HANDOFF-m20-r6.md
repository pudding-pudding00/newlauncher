# Übergabe: M20 – offener Punkt R6 (Wii-Spiel von USB starten)

Stand: 2026-11-25, 13:56. Branch `feat/m20-direktstart`, **11 Commits lokal vor** `origin/feat/m20-direktstart` (`2ec10c0`), noch nicht gepusht, kein PR offen bzw. nicht aktualisiert.

## 1. Was fertig ist (committet)

| Commit | Inhalt |
|---|---|
| `b2d276a` | `wii_tls` auf v0.2.0 (`3962086`), `WTLS_GZIP ON`, Workaround `z_errmsg=wtls_z_errmsg` |
| `cd4de0e` | HTTP-Client über `<wiitls/http.h>` (Keep-Alive, gzip, Redirects) – live auf der Wii geprüft (OSCWii `NL_EMU_LIST ok n=301`, GitHub `NL_SELF_UPDATE fail manifest`, weil noch kein Release existiert) |
| `a9cc075` | RemoteServer-Logging threadfest, Banner-Release bei Exit, `--remote` im Rückkehr-Stub |
| `8fb11a3` | `BTE_Shutdown()` vor `WPAD_Shutdown()` – behebt DSI-Absturz `PC=0x802061C4` (`__wpad_eventCB`) bei Beenden/Neustart/Start |
| `0e9c9b4` | Absturzbericht in MEM2 (`0x932F0000`, Magic `NLCR`), beim nächsten Start als `NL_PREV_CRASH` geloggt |
| `3c35a38` | `keepRemote` vor `remote.stop()` merken |
| `6e46362` | Rückkehr-Stub bei wiiload-Start aus dem RAM überspringen (`returnStubDol`) |
| `6c2d245`, `cab15a5`, `f04c314`, `219d4e7` | Abnahme-Doku |

**Hardware-Abnahme [m20-wii.md](acceptance/m20-wii.md):** R1, R2, R3 (3×), R4, R5, F1, U2 = **bestanden**.
U1 nicht gegen echtes GitHub prüfbar (`gh release list` auf `Zernichter0/newlauncher` leer); Dolphin `--update-server --tls` grün.
**R6 = offen** (dieses Dokument).

## 2. Nicht committete Arbeitskopie (Diagnose-WIP, NICHT auf SD)

- `boot/source/wdvd.c` (Z. ~197–202): `WDVD_SetFragList` gibt den d2x-Rückgabewert aus `outbuf[0]` weiter, wenn `IOS_Ioctl` `1` liefert. **Echter Fix – behalten.**
- `boot/source/main.cpp`:
  - Fehlerprüfung jedes Schritts (`rWdvd`, `rBlk`, `rUsb`, `rOpen`, `rPart`, `rOpenPart`), statt bei Fehler nach `AppEntrypoint=0` zu springen (= ursprünglicher Schwarzbild-Bug). **Behalten.**
  - Retry-Schleife 24 × 250 ms um `Disc_SetUSB` bei `-4`. Hilft nachweislich nicht (alle 24 Versuche `-4`), **Verdacht: verursacht die Speicherkorruption** (siehe 4.).
  - `/dev/usb2`-Sonde (`IOS_Open` + `IOS_Ioctlv(0x554D5301)`) → `ehci=`. Diagnose, später entfernen.
  - `DiagBuf`-Formatter ohne printf, schreibt Diagnose nach MEM2 und per `CON_Init` auf den TV, wartet auf RESET.
  - `mAB=` liest `*(u32*)0xC0AB0040` (Diagnose, entfernen).
- `CMakeLists.txt` (Z. ~276–292): `nl_game_booter` mit `-fno-exceptions -fno-rtti -ffunction-sections -fdata-sections`, Link `-Wl,--gc-sections`.
- `docs/STATUS.md`: vorläufige Abschnitt-0-Änderung, muss am Ende sauber neu geschrieben werden.

**SD-Karte der Wii** (`sd:/apps/newlauncher/boot.dol`) = Stand `6e46362`. Alle Diagnose-Builds liefen nur per `wiiload` im RAM.

## 3. R6-Fehlerbild

Testspiel: *Animal Crossing Wii* `RUUE01` (NTSC-U; alle 55 Wii-Spiele der Platte sind NTSC-U, Konsole ist PAL 4.3E).
Ursprünglich: kurzer Balken unten, dann dauerhaft schwarz.

Diagnose (Build #2, ohne Retry/Sonde, sauber angezeigt):

```
IOS=249 (cfg=249 type=0) dev=1 frags=3
wdvd=0 blk=0 usb=-4 open=0 part=0 openp=0
disc=000000000000 gameIOS=0 entry=00000000
```

→ IOS-Reload auf 249, `/dev/di`, `BlockIOSReload` (= Slot 249 ist wirklich d2x) und Übergabe der Fragmentliste gehen; **nur `WDVD_SetFragList` liefert `-4`**.
Laut d2x-Quellcode (`wiidev/d2x-cios`, `source/dip-plugin/frag.c` `Frag_Init`): `-4` = `usbstorage_Init()` fehlgeschlagen, d. h. das d2x-EHCI-Modul bekommt die Platte nicht initialisiert. Im `ehci-module` (`tinyehci/usbstorage.c`) liefert `USBStorage_Init` `-100`, wenn an **Port 0** nichts steckt, `-101` nach Port-Reset (Retry nötig).

Hardware-Aufbau laut Nutzer: Wii steht aufrecht, Platte steckt im (von vorne gesehen) **rechten** USB-Port. Der Launcher selbst (libogc/IOS58) sieht die Platte einwandfrei (`NL_DEV usb fat mounted lba=2048`, `games 808`).

## 4. Offenes Rätsel: Program-Exception nach Retry/Sonde

Sobald die Retry-Schleife (und die `/dev/usb2`-Sonde) im Build sind, stürzt der Booter **vor** der Diagnose-Ausgabe mit `Exception (Program)` (illegaler Befehl) ab – immer im Bereich `0x80AB00xx`:

| Build | Änderung | PC | Funktion dort | GPR23 |
|---|---|---|---|---|
| #3 | Retry + Sonde (`IOS_Ioctlv(..., NULL)`), snprintf | `0x80AB0040` | `_svfprintf_r` | – |
| #4 | printf durch `DiagBuf` ersetzt | `0x80AB0040` | `global_stdio_init.part.0` (aus `CON_Init`) | `0x18` = 24 Versuche, alle `-4` |
| #5 | Sonde mit `dummyVec` statt NULL, `--gc-sections` | `0x80AB0070` | **erster Befehl** von `global_stdio_init.part.0` (`stwu r1,-32(r1)`, im ELF korrekt) | `0x18` |

Build #5 Stack: `0x80ab0070 ← __sinit (0x80ab0268) ← setvbuf (0x80aaff8c) ← __set_default_console (0x80a9420c)` – also wieder aus `CON_Init`. `mAB`/`ehci` wurden deshalb nie angezeigt.

**Schluss:** Der Absturz wandert mit dem verschobenen Code nicht 1:1 mit, liegt aber stets im Fenster `0x80AB0000–0x80AB00FF`. Der Code dort ist im ELF gültig → **der Speicher bei physisch `0x00AB00xx` wird zur Laufzeit überschrieben** (oder der I-Cache ist veraltet), wahrscheinlich durch IOS/d2x-DMA während der 24 `Disc_SetUSB`-Wiederholungen bzw. der `/dev/usb2`-Sonde. Ohne Retry/Sonde (Build #2) lief derselbe `CON_Init`-Pfad fehlerfrei.

Bereits ausgeschlossen: `__IOS_ShutdownSubsystems()` unter `IRQ_Disable()` (wird bei Fehler nicht erreicht), printf-Pfad (Build #4 ohne printf stürzt genauso ab), `IOS_Ioctlv` mit NULL-Vektor (Build #5 mit `dummyVec` stürzt ebenso ab).

## 5. Empfohlene nächste Schritte

1. **Gegenprobe ohne newlauncher (wichtigste Frage):** Startet `RUUE01` von derselben Platte mit **USB Loader GX oder WiiFlow** über cIOS 249? Wenn nicht → Problem der Platte/des Ports/des cIOS, nicht unseres Codes.
2. **Port tauschen:** Platte in den anderen USB-Port stecken (d2x-EHCI nutzt standardmäßig nur Port 0) und R6 erneut testen.
3. **Korruption eingrenzen** (falls weiter nötig): je ein Build nur mit Retry bzw. nur mit Sonde; vor/nach jedem Schritt eine Prüfsumme über `0x80AB0000–0x80AB0100` bilden und in der Diagnose ausgeben; alternativ `ICInvalidateRange`/`DCFlushRange` über `.text` vor `CON_Init`. Prüfen, ob d2x einen Puffer bei `0x00AB0000` benutzt (Fragmentliste/`usbstorage`-Puffer).
4. Ggf. cIOS 250 (base 57) statt 249 probieren.
5. **Aufräumen:** Diagnose-Code (`mAB`, Sonde, ggf. Retry) entfernen; behalten: `wdvd.c`-Rückgabewert, Fehlerprüfung + sichtbare Fehlermeldung statt Sprung nach 0 (DE+EN), CMake-Flags. In [boot/UPSTREAM.md](../boot/UPSTREAM.md) dokumentieren, committen. SD-Update nur nach Rückfrage beim Nutzer.
6. Wenn R6 durch eine Hardware-/cIOS-Eigenheit nicht lösbar ist: in [m20-wii.md](acceptance/m20-wii.md) als bekannte Einschränkung dokumentieren, alternativ R6 mit einem **Kanal** (NAND, `NL_NAND channels=7`) abnehmen – R6 heißt „Wii-Spiel **oder** Kanal starten“.
7. Abschluss M20: [STATUS.md](STATUS.md) Abschnitt 0 neu schreiben (inkl. Nebenbefund unten), volle Prüfung (`python tools/build_wii.py`, `--no-tls`, beide 0 Warnungen; `python tools/check_licenses.py build/wii`; Host-Tests `--preset host` mit Kitware-CMake; `python -m unittest discover -s tools/tests`; Dolphin-Smokes `--return-stub`, `--remote-check`, `--update-server --tls`, `--cover-server --tls`, `--no-tls`), Push, PR, `gh run watch <id> --exit-status`, `gh pr merge <N> --merge` (kein Auto-Merge, kein Force-Push).

## 6. Nebenbefunde

- **M13 cIOS-Erkennung:** Slots 248–251 erscheinen in der Systeminfo als `other` statt `d2x`, weil `src/platform/wii_disc.cpp:230` die `.app`-Datei unter Stock-IOS58 ohne ISFS-Patches nicht lesen darf. Slot 249 ist nachweislich d2x.
- **Upstream `Zernichter0/wii_tls`:** `z_errmsg` kollidiert trotz `Z_PREFIX` (zlib 1.3.1) – hier per Define umgangen, upstream melden.

## 7. Arbeitsumgebung / Fallstricke

- Wii: `192.168.1.166`, SysMenu 4.3E PAL, Launcher-IOS 58, AHBPROT aus.
- Fernsteuerung: `python tools/wii_remote.py --host 192.168.1.166 state|exit|start|launch <ID6>` (`start` = wiiload des lokalen `build/wii/newlauncher.dol` in den RAM).
- **Niemals** `Test-NetConnection`/rohe TCP-Verbindungen auf HBC-Port 4299 – blockiert wiiload („Fehler beim Empfangen“).
- **Vor jedem Schreiben auf die SD der Wii den Nutzer fragen** (`put`, `update`, `hbc-install`, `wii_deploy.py`).
- MEM2-Absturzbericht überlebt keinen wiiload-Upload (HBC überschreibt oberes MEM2).
- PowerShell 5.1: kein `&&`; Dateien nicht mit `Get-Content`/`Set-Content` bearbeiten (UTF-8-Mojibake).
- Host-CMake: Kitware-CMake verwenden (MSYS2-CMake verträgt sich nicht mit WinLibs-Ninja).
- Booter-Disassemblierung: `C:\devkitPro\devkitPPC\bin\powerpc-eabi-objdump.exe -d --start-address=… build/wii/game_booter.elf`, Symbole mit `powerpc-eabi-addr2line -f -C -e …`. Booter liegt ab `0x80A80000`, `.text` bis `0x80AB9FA0`.
- Fotos der Diagnose-/Crash-Bildschirme liegen außerhalb des Repos im Antigravity-Artefaktordner dieser Unterhaltung (`media_1795608489320…` Build #2, `…608941337…` #3, `…609631708…` #4, `…610704196…` #5).
