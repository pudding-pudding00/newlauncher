# Hardware-Abnahme M18 (Banner in der Detailansicht)

Die Abnahme läuft auf einer echten Wii mit der Release-DOL (`python tools/build_wii.py`, 0 Warnungen, TLS an).

Dolphin hat schon gezeigt (`tools/dolphin_smoke.py`):
- **GC-Banner:** `open_scene=details:GM8P01` lädt das Banner aus dem eigenen GC-Abbild (`NL_BANNER gc ok GM8P01`).
- **Kanal-Banner aus `banner_file`:**
  - Die Fixture `banner.app` läuft animiert (`NL_BANNER budget=57548 level=anim`).
  - Der Ton startet nach 1 s Fokus (`NL_BANNER sound play`, `NL_SOUND banner`).
  - Die Musik wird gedämpft (`NL_MUSIC vol=30`).
  - Nach „Zurück“ (`details_back_ms`) folgen `NL_BANNER stop` und `NL_MUSIC vol=153`.
- **Budget:** `banner_big.app` (3 × 1024² C4) überschreitet 4 MiB und fällt aufs Cover (`NL_BANNER_FALLBACK cover budget`).
- **Wii-Spiel, nur die Verdrahtung:** `open_scene=details:RMCP01` mit der Kopf-Fixture liefert `NL_BANNER_FALLBACK cover invalid WBFS: Kopf`, ohne Absturz. Die echte Entschlüsselung über `ES_Decrypt` ist nur auf der Hardware prüfbar (W6).

Für jeden Punkt wird PASS, FAIL oder n/a eingetragen. Bei FAIL kommen eine kurze Notiz in die Zeile und ein Eintrag unter [Befunde](#befunde). Das Log liegt im Datenordner unter `newlauncher/log.txt`.

## Vorbereitung

1. Bauen: `python tools/build_wii.py`.
2. `newlauncher/config.ini` der Karte sichern.
3. In `config.ini` unter `[debug]` `perf = 1` und `log_sounds = 1` setzen (für W2 und W5).
4. Mindestens ein GC-Spiel (ISO oder CISO), ein Wii-Spiel als `.wbfs` oder `.iso` und drei installierte VC-Kanäle bereithalten. Die VC-Kanäle sollen eine Zuordnung in der VC-Liste haben.
5. Keine ROMs, ISOs, WADs oder Banner-Dateien ins Repo legen. Testdaten liegen nur auf den Datenträgern.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | GC-Spiel in der Bibliothek wählen, Details öffnen, 1 s warten; einmal mit Systemsprache Deutsch, einmal Englisch | Das Banner erscheint nach etwa 1 s statt des Covers. Titel und Beschreibung stehen in der Systemsprache. Log `NL_BANNER gc ok <ID6>` | ungeprüft |
| W2 | Emulator-Spiel mit VC-Zuordnung (NES, SNES oder N64) öffnen, 5 s warten, dann „Zurück“ | Das Banner ist animiert. Nach 1 s ist der Banner-Ton hörbar, die Musik deutlich leiser (`NL_MUSIC vol=…` gedämpft). Nach „Zurück“ verstummt der Ton sofort und die Musik ist wieder laut | ungeprüft |
| W3 | Drei verschiedene VC-Kanäle nacheinander öffnen | Die Darstellung ist plausibel: Bildteile an der richtigen Stelle, Farben stimmen, die Animation läuft im Kreis. Jede Abweichung mit Foto unter Befunde notieren | ungeprüft |
| W4 | In der Bibliothek schnell durch mehrere Spiele blättern, jeweils kurz die Details öffnen und gleich wieder schließen | Kein Ruckeln. Kein Ton aus einem schon verlassenen Spiel. Unter 1 s Fokus startet kein Ladevorgang (keine `NL_BANNER budget=`-Zeile) | ungeprüft |
| W5 | Details mit animiertem Banner 10 s offen lassen | `NL_PERF` meldet in der Detailansicht ≤ 16,7 ms je Bild | ungeprüft |
| W6 | Wii-Spiel (`.wbfs` oder `.iso` auf SD/USB) öffnen, 2 s warten | Das Banner des Spiels erscheint animiert, mit Ton. Log `NL_BANNER budget=… level=anim` oder `level=static`. Bei `NL_BANNER_FALLBACK cover io es` hat IOS `ES_Decrypt` abgelehnt; dann die laufende IOS-Version aus der Systeminfo unter Befunde eintragen | ungeprüft |

### Hinweise zu den Punkten

- **W2:**
  - Die Musiklautstärke `vol=` ist der ASND-Wert 0–255. Gedämpft heißt 20 % der eingestellten Lautstärke.
  - Ist `[audio] sounds = 0` gesetzt, bleibt der Banner-Ton aus. Das ist kein FAIL.
  - Ein Knacken an der Schleifenstelle des Tons bitte unter Befunde notieren.
- **W3:** Kanäle mit vielen Ebenen oder Spezialeffekten können vereinfacht aussehen.
  - Der Launcher zeichnet nur Bildfelder (`pic1`).
  - Text- und Rahmenfelder (`txt1`, `wnd1`) fehlen bekannt. Wenn sie stören, bitte unter Befunde eintragen.
- **W4:** Einstellungen → „Banner“ aus schaltet alle Banner ab. Danach zeigen die Details sofort nur das Cover.
- **W6:**
  - Geteilte Dateien (`.wbf1`) und Spiele direkt auf einer WBFS-Partition zeigen bewusst nur das Cover (Log `cover io WBFS geteilt` bzw. `cover io Wii: Dateityp`). Das ist kein FAIL.
  - Der Launcher kennt keinen Schlüssel. Der Titelschlüssel kommt aus IOS.
- **Nach der Abnahme** die gesicherte `config.ini` zurückspielen.

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
