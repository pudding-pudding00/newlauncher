# Hardware-Abnahme M23: Cover-Cache löschen, Zeiger-Scrollen, Kacheln im Cover-Format (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher (TLS-Build), WLAN, Retro-ROMs mit Covern).
Spec: `docs/superpowers/specs/2026-11-25-m23-cover-cache-zeiger-design.md`.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| C1 | Einstellungen → „Cover-Cache löschen“, einmal A | Wert zeigt „A: wirklich löschen?“, nichts gelöscht | ungeprüft |
| C2 | Nach C1 nach unten und zurück, dann B | Wert wieder leer, Einstellungen schließen, Cover bleiben | ungeprüft |
| C3 | „Cover-Cache löschen“, zweimal A | Log `NL_COVERS_CLEARED files=<n> failed=0`; alle Kacheln zeigen Platzhalter; `covers_src/<konsole>/` und `cache/covers/<konsole>/` leer, `missing.txt` weg | ungeprüft |
| C4 | C3 während eines laufenden Cover-Downloads | Download bricht ab, kein Absturz, danach keine neuen Dateien | ungeprüft |
| C5 | Nach C3 „Fehlende Cover laden“ | alle Cover werden neu geladen | ungeprüft |
| Z1 | Konsolenliste mit mehr als 3 Reihen, Zeiger an den unteren Rand des Rasters | Raster scrollt stetig nach unten, am Rand schneller; Fokus auf der Kachel unter dem Zeiger | ungeprüft |
| Z2 | Zeiger an den oberen Rand des Rasters | scrollt zurück bis zur ersten Reihe | ungeprüft |
| Z3 | Zeiger bis zum Ende | stoppt, wenn die letzte Reihe ganz sichtbar ist | ungeprüft |
| K1 | SNES- bzw. N64-Liste | flache Kacheln (halbe Höhe), Cover füllen sie quer | ungeprüft |
| K2 | GB-, GBC-, GBA-Liste | quadratische Kacheln | ungeprüft |
| K3 | Wii-, GC-, NES-Liste, Favoriten, Zuletzt | Kacheln wie bisher (5:7); Retro-Cover darin unverzerrt eingepasst | ungeprüft |
| K4 | Detailansicht eines SNES-Spiels | Cover unverzerrt (quer) | ungeprüft |
| P1 | `[debug] perf = 1`, „Fehlende Cover laden“ | Log je Cover `NL_PERF cover get status=200 ms=… hs=… bytes=… reuse=1` und `NL_PERF cover save ms=…` | ungeprüft |
