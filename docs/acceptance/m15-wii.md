# Hardware-Abnahme M15 (Cheats, GCT, WIP)

Die Abnahme läuft auf einer echten Wii mit der Release-DOL (`python tools/build_wii.py`, 0 Warnungen, TLS an).

Dolphin hat schon gezeigt:
- Die Cheat-Szene lädt, wählt und speichert Codes (`<data>/codes/<ID6>.txt` und `.sel`).
- Im Trockenlauf geht die GCT an den Game-Booter (`NL_WII_CHEATS bytes=24 hook=1`).
- Die WIP-Liste geht an den Game-Booter (`NL_WII_WIP n=3 skipped=1`, kaputte Zeile als `NL_WIP_SKIP line=5`).
- Nintendont bekommt `CHEATS|CHEAT_PATH` (`NL_NIN_CFG flags=00000099`) und die GCT liegt auf dem Spielgerät (`NL_NIN_CHEAT sd:/newlauncher/cache/<ID6>.gct`).

Ob der Codehandler im laufenden Spiel greift, zeigt erst die Hardware. Für jeden Punkt wird PASS, FAIL oder n/a eingetragen. Bei FAIL kommen eine kurze Notiz in die Zeile und ein Eintrag unter [Befunde](#befunde). Das Log liegt im Datenordner unter `newlauncher/log.txt`.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. `newlauncher/config.ini`, `newlauncher/games.ini` und den Ordner `newlauncher/codes/` der Karte sichern.
3. Code-Quelle: `[cheats] source` in `config.ini`; leer gilt der Standard. Der Download startet nur von Hand in der Cheat-Szene, nie automatisch.
4. Die Cheat-Szene öffnet sich in den Spieldetails über die Zeile „Cheats …“ (A). Die Option „Cheats“ (und für Wii „Cheat-Hook“) steht in den Spieloptionen.
5. Keine ROMs, ISOs oder WADs ins Repo legen; Testdaten liegen nur auf den Datenträgern. Code- und WIP-Dateien aus fremden Quellen bleiben ebenfalls auf der Karte.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | Wii-Spiel: in der Cheat-Szene Codes laden, zwei einfache Codes wählen (z. B. Leben, Münzen), Option „Cheats“ = An, starten | Log `NL_WII_CHEATS bytes=… hook=1`; das Spiel startet; beide Codes wirken | |
| W2 | Spiel, das mit Hook `vbi` hängt oder dessen Codes nicht greifen (falls bekannt): „Cheat-Hook“ = `kpad` | Log `hook=2`; das Spiel läuft und die Codes wirken | |
| W3 | GC-Spiel über Nintendont: Codes wählen, GC-Option „Cheats“ = An, starten | Log `NL_NIN_CHEAT <gerät>:/newlauncher/cache/<ID6>.gct` und `NL_NIN_CFG` mit Bit 0 und 4; die Codes wirken | |
| W4 | GC-Spiel auf `usb2` mit Codes (nur wenn eine zweite Partition vorhanden ist) | Derzeit: Dialog „Gerät nicht unterstützt“ (GC nur auf der ersten Partition, M12); keine GCT wird geschrieben | |
| W5 | Wii-Spiel mit `<data>/codes/<ID6>.wip`, die einen bekannten, sichtbaren Patch enthält | Log `NL_WII_WIP n=…`; Booter-Log (USB Gecko, falls vorhanden) „WIP: … Address Patched.“; die Änderung ist im Spiel sichtbar | |
| W6 | Viele Codes wählen, bis die Liste zu groß ist (Wii: über 3272 Bytes, GC: über 3072 Bytes), starten | Dialog „Zu viele Cheats“ mit der Größe in Bytes; das Spiel startet nicht | |

### Hinweise zu den Punkten

- **W1:** Codes mit Platzhaltern (`XXXX`) lassen sich nicht wählen. Ein Code aus der `.sel`, der nach einem neuen Download fehlt, fällt still weg.
- **W2:** Die Hook-Nummer im Log folgt der Reihenfolge `vbi`=1, `kpad`=2, `joypad`=3, `gxdraw`=4, `gxflush`=5, `ossleep`=6, `axframe`=7. Ohne gewählte Codes steht kein `NL_WII_CHEATS` im Log (hooktype 0).
- **W3:** Nintendont ignoriert eine zu große GCT still. Wirken die Codes nicht, im Log die Byte-Zahl notieren; der Preflight lässt höchstens 3072 Bytes zu.
- **W5:** WIP-Offsets zählen im Datenstrom der main.dol, nicht als `0x80…`-Adresse. Zeilen mit solchen Adressen erscheinen als `NL_WIP_SKIP line=…`.
- **Nach der Abnahme** die gesicherten Dateien zurückspielen und `newlauncher/cache/*.gct` auf dem Spielgerät löschen.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
