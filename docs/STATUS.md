# Projektstand (Übergabe)

Stand: 2026-11-25, `main` nach PR #28 (M19); M20 läuft auf `feat/m20-direktstart` (Abschnitt 0). Regeln für Agenten stehen in [AGENTS.md](../AGENTS.md).

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

Die CI (`.github/workflows/build.yml`) baut Wii mit und ohne TLS und führt die Host-Tests aus.

## Offen

Alle Code-Meilensteine (M0–M19) der v2-Spec sowie die offenen Review-Kleinigkeiten sind umgesetzt; offen ist die Hardware-Abnahme auf echter Hardware.

### 0. M20 in Arbeit (Branch `feat/m20-direktstart`, Stand 2026-11-25)

Ziele (Spec `docs/superpowers/specs/2026-11-25-m20-direktstart-design.md`, Plan
`docs/superpowers/plans/2026-11-25-m20-rueckkehr-stub.md`, Abnahme `docs/acceptance/m20-wii.md`):

1. ROM-Einträge starten den Emulator direkt mit der ROM.
2. Wer den Emulator beendet, landet wieder in newlauncher (Rückkehr-Stub).
3. Update per Netzwerk über den HBC, Fernstart und Fernsteuerung zum Debuggen.
4. Update und Neustart aus newlauncher aus den GitHub-Releases von `pudding-pudding00/newlauncher`.

**Fertig und auf Hardware geprüft:**

- Fernsteuerung über TCP 4300 (`tools/wii_remote.py`, Host über `--host` oder `NL_WII_HOST`): `hello`,
  `state`, `games`, `log`, `key`, `shot`, `launch <key>`, `update`, `restart`. Sie läuft nur mit dem Argument
  `--remote` (z. B. `wiiload boot.dol --remote`) oder mit `[debug] remote = 1`.
- SHOT liefert seit dem Fix in `remote_server.cpp` korrekte Bilder (blockierendes Senden über 4-KB-Puffer).
- `update` schreibt boot.dol auf die SD und startet neu, mit USB (698 Spiele).
- Einstellungen → „Update installieren“ meldet „Keine Update-Information erreichbar“: Es gibt noch kein
  Release (`NL_NET_FAIL other`).
- „newlauncher neu starten“ startet neu.
- Pfeil-Glyphen in der Hinweiszeile sind behoben (`d9b5184`).
- **Rückkehr-Stub (R2):** Emulator → „Exit“ → newlauncher erscheint wieder (Wii, 2026-11-25, mit `14284c7`).

**Rückkehr-Stub, Aufbau** (`src/launch/return_stub/`, `src/core/return_stub.*`, `src/launch/return_stub.cpp`):

- Vor dem Chainload einer Emulator-DOL legt `installReturnStub(selfDol)` einen frei stehenden Stub bei
  0x80001800 ab („STUBHAXX“ bei +4, libogc springt bei `exit()` dorthin), dazu einen Parameterblock bei
  0x80002800 mit den FAT-Extents von `sd:/apps/newlauncher/boot.dol`. Fortschrittscode bei 0x80002FF0.
- Der Stub schließt IOS-fds, öffnet `/dev/sdio/slot0`, liest die DOL sektorweise, lädt sie und startet sie
  mit argv. Bei Fehlern gibt es einen PI-Reset ins Systemmenü.
- Beim Start prüft newlauncher `returnStubInstalled()` → Log `NL_RETURN_STUB present stage=N`. Beendet man
  newlauncher bei liegendem Stub, startet es den HBC als Titel statt `exit()`.
- Nur, wenn newlauncher von `sd:/` läuft. Ein wiiload-Start liefert in argv[0] ebenfalls
  `sd:/apps/newlauncher/boot.dol`, deshalb legt auch er den Stub (der dann die **SD**-Version lädt).
- Fallen, die schon gelöst sind (Details in der Spec 4.2/4.5):
  - libogc `build_argv` schreibt argv[] hinter die Kommandozeile → Freiraum `NLRS_ARGV_SLACK`.
  - Dolphin-HLE „HBReload“ ersetzt den Sprung nach 0x80001800 → Smoke mit `EnableCheats=True`.
  - Dolphin-HLE an 0x800018A8 zählt das Wort bei 0x80001800 hoch → Lücke bis 0xB0 in `crt0.S`.
  - IOS-Pfade aus `.rodata`, nicht aus ungecachtem MEM2.
- Smoke: `python tools/dolphin_smoke.py --return-stub` (Marker `NL_RETURN_STUB present stage=0`,
  `NL_AUTOSTART skip return_stub`).

**Aktuelles Problem: USB nach Programmwechsel tot** (Hardware, 2026-11-25):

- Symptom 1: GBC-Eintrag (VBA GX, ROM auf USB) startet nur das VBA-GX-Menü, nicht das Spiel. VBA GX bindet USB
  erst beim Autoload ein (`argv[1]` = `usb:/…/`, `argv[2]` = Datei) und fällt bei Fehlern stumm ins Menü.
  Das passiert auch nach frischem Start aus dem HBC.
- Symptom 2: Nach der Rückkehr über den Stub findet newlauncher kein USB.
- Befund (Disassembly von `libogc.a`): Kein Abschaltpfad (`__usbstorage_Shutdown`, `SYS_ResetSystem`,
  `__IOS_ShutdownSubsystems`) ruft `USB_Deinitialize()` auf. Erst das gibt die IOS-Host-Treiber frei
  (`IOS_Ioctl(fd, 2 /*USBV5 SHUTDOWN*/)` + `IOS_Close` auf `/dev/usb/ven` und `/dev/usb/hid`).
- Fix, **gebaut, Dolphin-Smoke grün, auf der Wii noch nicht geprüft**:
  - `platform/storage.cpp` `unmountAll()`: zusätzlich `USB_Deinitialize()` (für den Weg zum Emulator und den
    Neustart; `__usbstorage_Startup` ruft `USB_Initialize` wieder auf).
  - `stub.c` `usbHostsShutdown()`: öffnet `/dev/usb/ven` und `/dev/usb/hid`, schickt ioctl 2 und schließt.
    Das läuft vor und nach der fd-Schleife, weil unklar ist, ob IOS einen zweiten Handle zulässt.
- **Vorbehalt:** Der Neustart newlauncher → newlauncher fand USB schon *ohne* `USB_Deinitialize`. Die Ursache
  ist also plausibel, aber nicht bewiesen. Hilft der Fix nicht, als Nächstes prüfen:
  - welche libogc bzw. welchen USB-Code VBA GX nutzt;
  - ob ein `IOS_ReloadIOS` vor dem Chainload hilft. Achtung: Der schaltet AHBPROT wieder ein, und newlauncher
    nutzt `ahbprotOff`.

**Nächste Schritte:**

1. Hardware-Test des USB-Fixes ohne SD-Schreiben:
   - Wii im HBC: `wiiload build/wii/newlauncher.dol --remote` (`WIILOAD=tcp:192.0.2.10`).
   - Dann `python tools/wii_remote.py launch "gbc/Azure Dreams (USA) (SGB Enhanced) (GB Compatible).gbc"`.
   - Erwartet: Das Spiel startet direkt, nach „Exit“ erscheint newlauncher, und USB ist da.
   - Der Stub kommt aus der wiiload-DOL, die geladene newlauncher von der SD.
2. Klappt das, die neue boot.dol auf die SD bringen (Nutzer vorher fragen) und R1–R6 aus
   `docs/acceptance/m20-wii.md` abhaken. Der Stub reicht `--remote` nicht weiter; für Beobachtung nach der
   Rückkehr `installReturnStub` um Zusatz-Argumente erweitern (wie `restartSelf`).
3. Offener Punkt aus früheren Tests: newlauncher stürzte bei Exit/Neustart-Tests ab, Ursache unklar
   (Log `usb:/newlauncher/log.txt` bzw. `sd:/newlauncher`). Möglicherweise dasselbe USB-Problem.
4. Abnahme und STATUS nachtragen, PR, CI (`gh run watch`), Merge.

**Hinweise für die Arbeit:**

- Wii: 192.0.2.10, HBC/wiiload auf Port 4299, Fernsteuerung auf Port 4300. Vor jedem Schreiben auf die SD
  den Nutzer fragen.
- Dolphin emuliert kein USB-Laufwerk: USB-Fehler zeigen sich nur auf der Hardware.
- Dateien mit dem Edit-Tool bearbeiten, nicht mit PowerShell `Get-Content`/`Set-Content`. PS 5.1 liest UTF-8
  ohne BOM als ANSI und erzeugt Mojibake.

### 1. Hardware-Abnahme auf der echten Wii

Bisher ist **kein** Punkt abgehakt, die Spalte „Ergebnis“ ist überall leer bzw. `ungeprüft` (gesamt 125 Punkte).

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
  - Homebrew Channel ist installiert. IP im LAN zuletzt **192.0.2.10**, wiiload auf TCP 4299.
- **Letzter Versuch (2026-11-24):** `python tools/wii_deploy.py --wiiload 192.0.2.10` brach mit „transfer failed“ ab, und die Wii war danach nicht mehr erreichbar.
  - Vermutlich war der HBC nicht im Vordergrund oder das WLAN schwach. Schon vorher gab es 50 % Paketverlust beim Ping.
  - Es lief nichts auf der Wii, und auf der SD-Karte wurde nichts geändert.
- **Nächster Schritt:** HBC öffnen, die IP unten rechts prüfen und den Versand wiederholen.
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
- Host-Tests unter Windows brauchen den WinLibs-GCC und Ninja (siehe README). Die CI führt sie unter Linux aus.
- Ein alter `build/wii`-Cache ist unproblematisch: Das CMake setzt die wii_tls-Krypto-Einstellungen per FORCE.
