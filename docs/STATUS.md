# Projektstand (Übergabe)

Stand: 2026-11-25, M20 (Direktstart, Rückkehr, Fernsteuerung, Selbst-Update) abgeschlossen. Regeln für Agenten stehen in [AGENTS.md](../AGENTS.md).

## Erledigt

| Meilenstein | Inhalt | PR |
|---|---|---|
| M0–M10 | v1: Launcher, Bibliothek, Booter, Emulatoren, Cover, Leistung (Spec `2026-11-09-wii-launcher-design.md`) | bis #13; v2-Spec #14 |
| M11 | Lizenz und TLS (wii_tls vendort, HTTPS-Standardquellen) | #15 |
| M12–M17 | Bibliothek universell, System-Erkennung, Spieloptionen, Cheats, Dateisysteme, Komfort | bis #23 |
| M18 | Banner: GC statisch, Kanal-Banner mit Animation und Ton | #24, Kleinigkeiten #25 |
| – | wii_tls auf Release **v0.1.0** (`9b1689b`); errno-Fix ist upstream | #26 |
| – | Übergabe-Dokumentation (`AGENTS.md`, `STATUS.md`, `CLAUDE.md`) | #27 |
| M19 | Weitere Startwege: optische Discs (Wii & GC über Nintendont), EmuNAND & Kanäle, vWii-Feinschliff | #28 |
| – | Emulator-Verwaltung „Alle installieren“, WLAN-Zustand in der Infoleiste | #30, #31 |
| M20 | Direktstart von ROMs, Rückkehr-Stub, Fernsteuerung (TCP 4300), Selbst-Update, wii_tls **v0.2.0** mit HTTP/1.1-Client | Branch `feat/m20-direktstart` |

Die CI (`.github/workflows/build.yml`) baut Wii mit und ohne TLS und führt die Host-Tests aus.

## Offen

### 0. Nach M20

Spec `docs/superpowers/specs/2026-11-25-m20-direktstart-design.md`, Abnahme
[acceptance/m20-wii.md](acceptance/m20-wii.md): R1–R6, F1, U2 auf der Wii **bestanden**.

- **U1 ungeprüft:** Update aus den GitHub-Releases braucht ein Release von `pudding-pudding00/newlauncher`.
  Bisher gibt es keins (`NL_SELF_UPDATE fail manifest`). Gegen den Dolphin-Smoke `--update-server --tls` ist es grün.
- **R6, Einschränkung USB-Port:** Das EHCI-Modul von d2x (cIOS 249) liest nur USB-Port 0. Steckt die Platte am
  anderen Port, liefert `WDVD_SetFragList` `-4`. Der Booter zeigt dann eine Fehlermeldung mit Hinweis auf den
  Port (DE+EN), statt schwarz zu bleiben. Der Launcher (IOS 58) liest beide Ports. Details in `boot/UPSTREAM.md`.
- **Offenes Rätsel (nicht mehr relevant für R6):** Mit einer Wiederholungsschleife um `Disc_SetUSB`
  (24 × 250 ms) bzw. einer `/dev/usb2`-Abfrage stürzte der Booter vor der Fehleranzeige mit „Program“-Exception im
  Bereich `0x80AB00xx` ab (in `CON_Init`). Der Code dort ist im ELF gültig, also wurde der Speicher zur Laufzeit
  überschrieben. Beides ist wieder entfernt. Wer im Booter künftig länger wartet oder `/dev/usb2` anspricht, muss
  das zuerst klären.
- **Nebenbefund M13:** cIOS-Slots 248–251 erscheinen in der Systeminfo als `other` statt `d2x`.
  `src/platform/wii_disc.cpp` darf die `.app`-Datei unter Stock-IOS 58 ohne ISFS-Patches nicht lesen.
  Slot 249 ist nachweislich d2x (`BlockIOSReload` gelingt).
- **Upstream `pudding-pudding00/wii_tls`:** `z_errmsg` kollidiert trotz `Z_PREFIX` (zlib 1.3.1). Hier per Define
  `z_errmsg=wtls_z_errmsg` umgangen, upstream melden.
- **Wunsch des Nutzers (2026-11-25):** Im Wii-Spiel „Wii-Menü“ soll zurück in newlauncher führen. Geht nur über
  `returnTo` auf eine Title-ID, also mit einem Forwarder-Kanal auf dem NAND. Der steht in Spec v1 und v2 unter
  „Nicht-Ziele“. Heute möglich: `return_to = hbc` (Rückkehr in den HBC).

**Hinweise für die Arbeit:**

- Wii: 192.0.2.10, HBC/wiiload auf Port 4299, Fernsteuerung auf Port 4300
  (`python tools/wii_remote.py --host 192.0.2.10 state|start|launch <ID6>|…`). Vor jedem Schreiben auf die SD
  den Nutzer fragen.
- **Niemals** rohe TCP-Verbindungen auf Port 4299 (z. B. `Test-NetConnection`): Das blockiert wiiload.
- Der MEM2-Absturzbericht (`NLCR` bei `0x932F0000`, im Log `NL_PREV_CRASH`) überlebt keinen wiiload-Upload.
- Dolphin emuliert kein USB-Laufwerk: USB-Fehler zeigen sich nur auf der Hardware.
- Booter-Disassemblierung: `powerpc-eabi-objdump -d build/wii/game_booter.elf`. Der Booter liegt ab `0x80A80000`.
- Dateien mit dem Edit-Tool bearbeiten, nicht mit PowerShell `Get-Content`/`Set-Content`. PS 5.1 liest UTF-8
  ohne BOM als ANSI und erzeugt Mojibake.

### 1. Hardware-Abnahme auf der echten Wii

Abgehakt ist bisher nur M20 (bis auf U1). In den übrigen Listen ist die Spalte „Ergebnis“ überall leer bzw. `ungeprüft` (125 Punkte).

| Liste | Punkte |
|---|---|
| [hardware-acceptance.md](hardware-acceptance.md) (v1/M7, mit Leistungsbudgets) | 65 |
| [acceptance/m11-wii.md](acceptance/m11-wii.md) bis [m19-wii.md](acceptance/m19-wii.md) | 8 + 7 + 6 + 7 + 6 + 6 + 7 + 6 + 7 = 60 |

- **Pflicht-Aufbauten** (Spec v2, Abschnitt 13):
  - Wii mit USB-Platte in FAT32;
  - Wii ohne SD-Karte;
  - zweite USB-Partition, wenn die Platte das zulässt.
- **Optional:** vWii, Wii mini, NTFS, ext4, WBFS.
- Was ungeprüft bleibt, kommt als „ungeprüft“ in die Release-Notes.
- **Test-Wii:**
  - System-Menü **4.3E** (PAL), MAC-Adresse **00-00-00-00-00-00**.
  - Homebrew Channel ist installiert, IP im LAN **192.0.2.10**, wiiload auf TCP 4299. AHBPROT aus,
    cIOS 249 = d2x. USB-Platte (FAT32, 808 Spiele) am USB-Port 0.
  - Für die echte Abnahme braucht der Nutzer am Controller eine vorbereitete SD-Karte. Wie das geht, steht unter „Vorbereitung“ in `acceptance/m11-wii.md`; vor jedem Schreiben auf die Karte fragen.
  - Die Hardware-Suiten von wii_tls sind laut Release v0.1.0 schon auf einer Wii gelaufen. Wiederholen auf dieser Wii: siehe `acceptance/m11-wii.md`.

### 2. Bewusste Eigenheiten / verbleibende Hinweise (M17)

- **Kindersicherung (`[debug] pin_input`):** Hält die Test-PIN für automatisierte Dolphin-Smoke-Tests in `config.ini` (für den normalen Nutzerbetrieb auf der Wii nicht relevant).
- **Benchmark:** Der 5000er-Benchmark läuft bewusst nur im Release-Build.

### 3. Bekannte Grenzen der Banner (M18), mögliche spätere Ausbaustufen

- Text- und Fensterflächen (`txt1`/`wnd1`) werden nicht gezeichnet.
- Texturen werden immer mit `GX_CLAMP` gezeichnet.
- Gedreht wird nur um die z-Achse.
- `.wbf1`-Dateien und Spiele auf WBFS-Partitionen zeigen nur das Cover.

## Umgebung (Windows-Rechner des Nutzers)

- Repo: `<repo-root>`.
- Daneben liegen `..\wii_tools\Dolphin-x64` und `..\wii_tools\dolphin-user`, der Dev-User für Dolphin.
- devkitPro unter `C:\devkitPro`, wiiload unter `C:\devkitPro\tools\bin\wiiload.exe`.
- Host-Tests unter Windows brauchen den WinLibs-GCC und Ninja (siehe README) sowie Kitware-CMake
  (WinGet-Paket `Kitware.CMake`; das MSYS2-CMake verträgt sich nicht mit WinLibs-Ninja). Die CI führt sie unter Linux aus.
- Ein alter `build/wii`-Cache ist unproblematisch: Das CMake setzt die wii_tls-Krypto-Einstellungen per FORCE.
