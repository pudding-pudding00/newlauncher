# Hardware-Abnahme M14 (Spieloptionen)

Die Abnahme läuft auf einer echten Wii mit der Release-DOL (`python tools/build_wii.py`, 0 Warnungen).

Dolphin hat schon gezeigt:
- Die Options-Szene öffnet pro Spiel und global (`[debug] open_scene = options:<ID6>|wii|gc`).
- Ein gesetzter Wert landet in `games.ini` bzw. `config.ini` (`option_set`, `--expect-games-ini`).
- Im Trockenlauf geht die Wii-Startkonfiguration an den Loader.
- Nintendont bekommt die NIN_CFG-Flags (`NL_NIN_CFG`).

Ob ein Spiel die Optionen wirklich umsetzt, zeigt erst die Hardware. Das gilt für Videomodus, Sprache, Wiimmfi, den IOS-Slot und die Rückkehr. Für jeden Punkt wird PASS, FAIL oder n/a eingetragen. Bei FAIL kommen eine kurze Notiz in die Zeile und ein Eintrag unter [Befunde](#befunde). Das Log liegt im Datenordner unter `newlauncher/log.txt`.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. `newlauncher/config.ini` und `newlauncher/games.ini` der Karte sichern, z. B. auf den PC kopieren.
3. Die Werte lassen sich in der Szene ändern oder direkt in die Datei schreiben:
   - in der Szene pro Spiel mit Details → `+` bzw. GC-Controller `R`, global mit Einstellungen → „Wii-Spiele“ bzw. „GameCube-Spiele“;
   - direkt in der Datei unter `[game <ID6>]` in `games.ini` bzw. unter `[wii]`/`[gc]` in `config.ini`.
4. Für W6 wird ein 480p-fähiges Kabel (Component) gebraucht; ohne dieses Kabel ist der Teil `progressive` n/a.
5. Keine ROMs, ISOs oder WADs ins Repo legen. Testdaten liegen nur auf den Datenträgern.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | PAL-Wii-Spiel, pro Spiel „Videomodus“ = PAL 60, starten | Das Spiel läuft in 60 Hz (TV-Info bzw. flüssigeres Bild); Log `NL_LAUNCH` ohne Warnung | |
| W2 | Deutsches Spiel, „Sprache“ = Englisch | Die Spieltexte sind englisch | |
| W3 | Mario Kart Wii, „Online-Dienst“ = Wiimmfi (nur mit Netz) | Die Online-Verbindung geht zu Wiimmfi, nicht auf eine Fehlermeldung des alten Nintendo-Servers | |
| W4 | „IOS“ = cIOS 250 (nur wenn installiert) | Das Spiel startet über Slot 250 (Log `NL_WII_IOS 250 forced`) | |
| W5 | „Rückkehr zu“ = Homebrew Channel; Spiel starten, dann über das HOME-Menü des Spiels beenden | Die Wii landet im Homebrew Channel, nicht im Wii-Menü | |
| W6 | GC-Spiel: „Breitbild erzwingen“ = An, „Kartengröße“ = 1019 Blöcke, „Progressiv (480p)“ = An | Das Bild ist breit; Nintendont legt eine größere Speicherkarte an (Dateigröße unter `saves/`); mit 480p-Kabel läuft das Bild progressiv | |
| W7 | In der Szene des Spiels aus W1 „Auf Standard zurücksetzen“, mit B schließen, erneut starten | `[game <ID6>]` fehlt in `games.ini`; andere Abschnitte (z. B. `[n64_method]`) sind unverändert; das Spiel startet mit den globalen Werten | |

### Hinweise zu den Punkten

- **W1:** Steht in der Szene „Standard (…)“, gilt der globale Wert. Erst ein eigener Wert macht die Option spielbezogen.
- **W4:** Ein Slot, der fehlt, ergibt einen Dialog mit der Slotnummer und keinen Absturz (wie in der Abnahme von M13).
- **W6:** Gesperrte GC-Zeilen nennen die nötige Nintendont-Version („ab Nintendont …“). Eine Option ist nur gesperrt, wenn die erkannte Version älter ist. Eine unbekannte Version sperrt nichts.
- **W7:** Kommentare in `games.ini` und `config.ini` bleiben beim Speichern aus der Szene erhalten. Nur die Zeilen der geänderten Option ändern sich.
- **Nach der Abnahme** die gesicherten Dateien zurückspielen.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
