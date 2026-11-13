# Hardware-Abnahme M16 (NTFS, WBFS-Partition, Preflight je Dateisystem)

Die Abnahme läuft auf einer echten Wii mit der Release-DOL (`python tools/build_wii.py`, 0 Warnungen, TLS an).

Dolphin hat schon gezeigt (SD als Platte, `tools/dolphin_smoke.py --sd-disk …`):
- NTFS hängt nur lesend als `usb2` ein (`NL_DEV usb2 ntfs mounted ro`).
- Ein zusammenhängendes Wii-ISO auf NTFS liefert eine Fragmentliste in 512-Byte-Sektoren (`NL_FRAG dev=usb2 …`, `NL_FRAG_VERIFY ok`). Eine residente Datei (512 B) endet mit `NL_PREFLIGHT_FAIL FileNotContiguousReadable`.
- Eine WBFS-Partition hängt als `usb2 wbfs mounted ro` ein; alle Slots erscheinen als `NL_GAME <ID6> usb2 path=wbfs:<slot>`; ein Slot mit kaputtem Disc-Kopf wird mit `NL_WBFS_SKIP <slot> dev=usb2` übersprungen; der Trockenlauf für Slot 1 endet mit `NL_WII_DRYRUN`.

Ob der Game-Booter die Sektoren auf echter Hardware liest, zeigt erst die Wii. Für jeden Punkt wird PASS, FAIL oder n/a eingetragen. Bei FAIL kommen eine kurze Notiz in die Zeile und ein Eintrag unter [Befunde](#befunde). Das Log liegt im Datenordner unter `newlauncher/log.txt`.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. `newlauncher/config.ini` und `newlauncher/games.ini` der Karte sichern.
3. Eine USB-Platte vorbereiten (am PC, nicht aus dem Repo): Partition 1 FAT32, Partition 2 NTFS. Für W4 und W5 je eine weitere Partition (ext4 bzw. WBFS, z. B. mit Wii Backup Manager).
4. Keine ROMs, ISOs oder WADs ins Repo legen; Testdaten liegen nur auf den Datenträgern.
5. Die Systeminfo zeigt jedes Gerät mit Dateisystem und „nur lesend“ bzw. dem Grund, warum es nicht eingehängt ist.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | Platte mit FAT32 + NTFS anstecken, Systeminfo öffnen | NTFS-Partition erscheint als `usbN`, Dateisystem NTFS, nur lesend; Log `NL_DEV usbN ntfs mounted ro` | ungeprüft |
| W2 | Wii-Spiel als ISO und als WBFS-Datei unter `wbfs/` auf NTFS ablegen, beide starten | Beide erscheinen in der Bibliothek und starten; Log `NL_FRAG dev=usbN …` | ungeprüft |
| W3 | GC-Spiel unter `games/` auf NTFS ablegen und starten | Dialog „Nintendont braucht FAT32.“ mit Gerätename; Nintendont startet nicht | ungeprüft |
| W4 | ext4-Partition anstecken, Systeminfo öffnen | Gerät nicht eingehängt, Grund „ext wird nicht unterstützt“; keine Spiele von dort (Spike: kein ext-Treiber) | ungeprüft |
| W5 | WBFS-Partition mit mehreren Spielen anstecken, eines starten | Alle Spiele erscheinen (Log `NL_GAME <ID6> usbN path=wbfs:<slot>`); das gewählte startet | ungeprüft |
| W6 | Gemischte Platte (FAT32 + NTFS): Namen und Cover prüfen, Wii neu starten, erneut prüfen | Gleiche Titel und Cover nach dem Neustart; Spiele mit derselben ID6 auf beiden Partitionen erscheinen einmal (Quelle nach Gerätereihenfolge) | ungeprüft |

### Hinweise zu den Punkten

- **W2:** Komprimierte, sparse oder fragmentierte NTFS-Dateien starten nicht; der Dialog nennt „nicht zusammenhängend lesbar“. Abhilfe: Datei auf der Platte neu kopieren oder defragmentieren.
- **W5:** Auf 4-KiB-Platten rechnet der Launcher die Fragmente in 512-Byte-Sektoren um; ist eine solche Platte vorhanden, bitte zusätzlich prüfen und im Ergebnis vermerken.
- **Nach der Abnahme** die gesicherten Dateien zurückspielen.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
