# Hardware-Abnahme M17 (Classic Controller, Themes, Filter, Statistik, Kindersicherung)

Die Abnahme läuft auf einer echten Wii mit der Release-DOL (`python tools/build_wii.py`, 0 Warnungen, TLS an).

Dolphin hat schon gezeigt (`tools/dolphin_smoke.py`):
- Ein Theme-Paket mit kaputten Tokens lädt die gültigen Werte und meldet je Fehler `NL_THEME_BAD`.
- Der Filter `category:Party` auf der Leiste Favoriten zeigt genau ein Spiel (`NL_FILTER … n=1`).
- Ein Start zählt in `stats.ini` (`starts = 1`, `last = …`).
- Mit PIN `1234` und `lock_unrated = 1` lehnt `[debug] pin_input = 0000` den Autostart ab (`NL_PARENTAL denied`, kein `NL_LAUNCH`). Mit `1234` startet das Spiel (`NL_PARENTAL ok`, `NL_LAUNCH`). Die PIN steht in keinem Log.
- `use_wii = 1` liest Dolphins `SYSCONF` nur. Die Datei ist danach byte-gleich. Dolphin hat keine gültige IPL.PC-PIN, das Log zeigt `NL_PARENTAL_WII none`.

Für jeden Punkt wird PASS, FAIL oder n/a eingetragen. Bei FAIL kommen eine kurze Notiz in die Zeile und ein Eintrag unter [Befunde](#befunde). Das Log liegt im Datenordner unter `newlauncher/log.txt`.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. `newlauncher/config.ini`, `games.ini`, `categories.ini` und `stats.ini` der Karte sichern.
3. Für W3 ein Theme-Paket anlegen: `newlauncher/themes/<name>/theme.ini` und optional `background.png` (höchstens 640×480).
4. Keine ROMs, ISOs oder WADs ins Repo legen. Testdaten liegen nur auf den Datenträgern.
5. Für W7 vorher im Wii-Menü unter Einstellungen → Jugendschutz eine Stufe und eine PIN setzen. Der Launcher schreibt nie in `SYSCONF`; er liest nur.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | Classic Controller anstecken; alle Belegungen aus Spec 10.1 durchgehen; während der Navigation ab- und wieder anstecken | Jede Taste wirkt wie in Spec 10.1. Ab- und Anstecken lässt den Fokus stehen, nichts klemmt | ungeprüft |
| W2 | Classic Controller Pro, falls vorhanden; Wii-U-Pro-Controller optional | Wie W1. Beim Wii-U-Pro wirkt mindestens das Steuerkreuz, sonst n/a mit Notiz | ungeprüft |
| W3 | Theme-Paket in den Einstellungen wählen; hell und dunkel umschalten | Hintergrundbild unter den Kacheln; Farben aus `theme.ini`; beide Modi lesbar; kein `NL_THEME_BAD` bei gültiger Datei | ungeprüft |
| W4 | Im A–Z-Overlay nach Genre filtern, danach nach eigener Kategorie | Nur passende Spiele; Wechsel der Leiste setzt den Filter auf „Alle“ zurück | ungeprüft |
| W5 | Ein Spiel dreimal starten, zurückkehren, Sortierung „Meistgespielt“ wählen | Spiel steht vorn; Details zeigen „3-mal gestartet, zuletzt am …“ | ungeprüft |
| W6 | PIN `1234` setzen, Altersgrenze 12; ein Spiel mit PEGI 16 und eines mit `locked = 1` in `games.ini` prüfen; dreimal falsche PIN, dann die richtige | Beide Spiele tragen ein Schloss. Nach drei Fehlversuchen „Zu viele Versuche, bitte 30 s warten“, auch die richtige PIN wird 30 s lang abgelehnt. Danach startet das Spiel. Einstellungen → Kindersicherung und Einrichtung fragen ebenfalls die PIN. Die PIN steht nicht im Log | ungeprüft |
| W7 | „Wii-Jugendschutz übernehmen“ einschalten (Vorbereitung 5); ein Spiel über der Wii-Stufe starten, mit der Wii-PIN entsperren | Log `NL_PARENTAL_WII on system=<Stelle> age=<Alter>`. Die Stelle passt zur Wii-Einstellung (PAL: PEGI, US: ESRB). Das Alter ist die höchste erlaubte Stufe. Die Wii-PIN entsperrt; PIN-, Altersgrenze- und Entfernen-Zeile sind gesperrt | ungeprüft |

### Hinweise zu den Punkten

- **W6:** Die 30-s-Sperre zählt ab dem dritten Fehlversuch. Ein Neustart des Launchers hebt sie auf. Das ist bewusst so: Die Kindersicherung ist ein Kinderschloss, kein Tresor.
- **W7:** Die Zuordnung des Organisations-Bytes stammt aus der DSi-Spezifikation (Spike `docs/superpowers/spikes/m17-sysconf-pc.md`). Stimmt `system=` nicht mit der Wii-Einstellung überein, bitte den Rohwert aus dem Wii-Menü und die Logzeile unter Befunde eintragen. Zeigt das Log `NL_PARENTAL_WII none` trotz gesetzter Wii-PIN, ist das ein FAIL.
- **Nach der Abnahme** die gesicherten Dateien zurückspielen. Den Wii-Jugendschutz bei Bedarf im Wii-Menü wieder ausschalten.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
