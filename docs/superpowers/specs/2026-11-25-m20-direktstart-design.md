# M20: Direktstart, Rückkehr, Fernsteuerung, Selbst-Update

Stand: 2026-11-25. Ergänzt die v2-Spec (2026-11-11). Bindend für M20.

## 1. Ziele

1. ROM-Einträge (NES, SNES, GB/GBC/GBA, N64 …) starten die gewählte ROM direkt im Emulator (argv), nicht
   nur den Emulator.
2. Beendet man den Emulator (bzw. jede aus newlauncher gestartete Homebrew-DOL) über „Exit/Return to loader“,
   landet man wieder in newlauncher, nicht im Homebrew Channel.
3. Deployment über das Netz: newlauncher lässt sich per wiiload (HBC) in den RAM laden und mit `update` auf
   die SD schreiben.
4. Fernsteuerung (TCP 4300, nur mit `--remote` oder `[debug] remote = 1`) für Selbst-Debugging:
   HELLO, STATE, GAMES, LOG, SHOT, KEY, LAUNCH, PUT, RESTART, EXIT (`tools/wii_remote.py`).
5. In den Einstellungen: „Update installieren“ (GitHub-Release `update.ini`) und „newlauncher neu starten“.

## 2. Fernsteuerung (umgesetzt)

- Ein Client gleichzeitig. Zeilenprotokoll `core/remote_proto`, Antwortkopf mit Länge, danach Nutzdaten.
- Senden blockierend über einen 4-KB-Puffer in MEM1: nicht-blockierendes `net_send` zerhackt auf der
  Hardware große Antworten (SHOT, 600 KB).
- SHOT liefert den angezeigten XFB (YUYV, ungecacht gelesen).

## 3. Selbst-Update und Neustart (umgesetzt)

- `update.ini` (version, zip, zip_size, unzipped_size) liegt im Release neben dem ZIP; Standard-URL
  `https://github.com/pudding-pudding00/newlauncher/releases/latest/download/update.ini`. Nur HTTPS, ZIP ≤ 32 MiB,
  der ZIP-Name ist relativ zur Manifest-URL. Nur neuere Versionen werden installiert.
- Neustart = Chainload der eigenen boot.dol. Vorher fahren alle Treiber herunter, auch der USB-Treiber
  (`DISC_INTERFACE::shutdown`): ohne IOS-Neuladen findet der neue Prozess sonst kein USB-Laufwerk.

## 4. Rückkehr-Stub (Ziel 2)

### 4.1 Mechanik

libogc springt bei `exit()` nach 0x80001800, wenn dort bei +4 „STUBHAXX“ steht (HBC-Konvention). Vor dem
Chainload einer Homebrew-DOL (Emulator, Nintendont, App) legt newlauncher dort einen eigenen, frei stehenden
Stub ab, der newlauncher von der SD neu lädt.

- Speicher: Code 0x80001800–0x800027FF (≤ 4 KiB, Linker-ASSERT), Parameterblock 0x80002800–0x80002FFF.
- Nur wenn newlauncher selbst von der SD läuft (`self` beginnt mit `sd:/`) und die Extents seiner boot.dol
  ermittelbar sind (FAT32, `core/fat_chain`). Sonst bleibt der HBC-Stub unangetastet (Rückkehr wie bisher).
- Nicht bei Wii-Spielen und Kanälen (Codehandler nutzt 0x80001800, Spiele kehren nicht über den Stub zurück).

### 4.2 Parameterblock (big-endian, `core/return_stub`)

| Offset | Inhalt |
|---|---|
| 0x00 | Magic `NLRS` |
| 0x04 | Version 1 |
| 0x08 | DOL-Größe in Byte |
| 0x0C | Anzahl Extents |
| 0x10 | Länge des argv-Blocks (0 = keiner) |
| 0x14 | Prüfsumme: Summe der 32-Bit-Wörter von argv (aufgefüllt) und Extents, ohne den Freiraum |
| 0x18 | argv-Block (für 0x80002818 gepackt, auf 4 Byte aufgefüllt, ≤ 256 Byte) |
| danach | Freiraum `NLRS_ARGV_SLACK` (0x40 Byte): libogc `build_argv` legt hier beim Start der geladenen DOL `argv[]` ab (bei `(commandLine + Länge + 4) & ~3`); höchstens 14 Argumente |
| danach | Extents: je {lba, Sektoren}, lückenlos in Dateireihenfolge |

Ein Block, der nicht in 0x800 Byte passt oder dessen Extents nicht genau ceil(Größe/512) Sektoren abdecken,
wird nicht geschrieben (kein Stub, HBC-Rückkehr).

### 4.3 Ablauf im Stub

1. Magic, Version und Prüfsumme prüfen.
2. IPC von Hand (Polling, keine Interrupts). Alle IOS-Deskriptoren 0–31 schließen (der Emulator kann
   `/dev/sdio/slot0` offen gelassen haben).
3. `/dev/sdio/slot0` öffnen, RESETCARD (RCA), GETSTATUS (eingesteckt? SDHC?), Busbreite 1 Bit im
   Host-Controller, Takt setzen, SELECT, SETBLOCKLEN 512. Lesen mit READ_MULTIPLE_BLOCK (ioctlv SENDCMD),
   SDSC mit Byte-Adressen, SDHC mit Sektoradressen, in 32-KB-Stücken nach 0x92800000 (ungecacht gelesen).
4. DOL-Kopf prüfen (Sektionen innerhalb der Datei, Ziele in MEM1/MEM2), BSS nullen, Sektionen kopieren,
   Caches synchronisieren, argv in den libogc-Kopf, Gerät schließen, Einsprung.
5. Fehler: Fortschrittscode bei 0x80002FF0 bleibt stehen, dann Neustart der Konsole über PI-Reset
   (Systemmenü). newlauncher loggt einen vorgefundenen Code beim Start als `NL_RETURN_STUB stage=N` und
   löscht ihn.

### 4.4 Folgen in newlauncher

- „HOME → Homebrew Channel“ darf nicht mehr `exit()` nutzen, wenn unser Stub liegt (das würde newlauncher
  neu laden): dann HBC als Titel starten (`hbcReturnTo`), sonst Systemmenü.
- Neustart (Einstellungen/RESTART) lässt den Stub liegen.

### 4.5 Tests

- Host: Parameterblock (Layout, Grenzen, Prüfsumme), Prüf-Logik des Stubs (gemeinsame C-Header-Funktionen
  für Prüfsumme und DOL-Kopf).
- Dolphin: emuliert `/dev/sdio`; Smoke (`dolphin_smoke.py --return-stub`) startet `argv_echo.dol` mit
  `--exit` als Emulator, dessen `exit()` den Stub auslöst; Erfolg, wenn newlauncher danach
  `NL_RETURN_STUB present stage=0` und `NL_AUTOSTART skip return_stub` meldet.
- Dolphin-Eigenheiten (nur Emulator, nicht auf der Wii):
  - Die HLE „HBReload“ ersetzt den Sprung nach 0x80001800. Sie entfällt nur mit
    `Dolphin.Core.EnableCheats=True`, deshalb schaltet der Smoke Cheats ein.
  - An 0x800018A8 (Gecko-Einsprung) hängt immer eine HLE, die das Wort bei 0x80001800 hochzählt, sobald
    dort Code läuft. `crt0.S` lässt den Bereich bis Offset 0xB0 deshalb als nie ausgeführte Lücke frei.
  - Pfade für IOS_Open kommen aus `.rodata` in MEM1; byteweise nach ungecachtem MEM2 kopierte Pfade kamen
    leer an.
- Hardware: Emulator starten, im Emulator „Exit“, newlauncher erscheint; HOME → Homebrew Channel führt
  weiterhin in den HBC.
