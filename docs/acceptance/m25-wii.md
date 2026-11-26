# Hardware-Abnahme M25: WLAN robuster, genauer, mit Info-Fenster (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher (TLS-Build), WLAN-Router mit Zugang zur Oberfläche).
Spec: `docs/superpowers/specs/2026-11-26-m25-wlan-design.md`.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| W1 | Start mit funktionierendem WLAN | Symbol zeigt zuerst das Lauflicht (kein roter Punkt), dann 4 Balken; Log `NL_WIFI ok gw=yes gw_ms=<n>` | ungeprüft |
| W2 | Zeiger auf das WLAN-Symbol | Symbol bekommt einen hellen Hintergrund wie Home und Zahnrad | ungeprüft |
| W3 | Auf dem Symbol A drücken | Fenster „WLAN“ mit Zustand, IP, Netzmaske, Router, Router-Antwort (ms), MAC, verbunden seit, letzte Prüfung; Werte laufen mit; B schließt | ungeprüft |
| W4 | Im Fenster A („Neu verbinden“) | Hilfezeile „Verbinde neu …“, Symbol Lauflicht, danach wieder verbunden; „Neuaufbauten: 1“; Log `NL_CMD wifi_reconnect` | ungeprüft |
| W5 | Router ausschalten, während newlauncher läuft | Nach spätestens ~10 s gelber Punkt („eingeschränkt“) oder roter Punkt, Toast in der Infoleiste; Fenster zeigt Tipps | ungeprüft |
| W6 | Router wieder einschalten | Ohne Zutun wieder verbunden (Abstand der Checks 5/10/20/30 s), Toast „WLAN verbunden“ | ungeprüft |
| W7 | Start mit ausgeschaltetem Router | Toast „WLAN: keine Verbindung (Code …)“, roter Punkt; Fenster zeigt Fehlercode, Fehlschläge in Folge und Tipps | ungeprüft |
| W8 | Fall „WLAN kommt manchmal nicht hoch“: mehrmals hintereinander starten | Kommt es nicht sofort hoch, verbindet es sich ohne Neustart nach den Wiederholungen von selbst; Fehlercode notieren, falls nicht | ungeprüft |
| W9 | Cover laden (manuell) nach W4 | Download klappt (HTTPS nach Netz-Neuaufbau, `wtls_net_reset`) | ungeprüft |
| W10 | Fernsteuerung (`--remote`) aktiv, dann W4 | Verbindung bricht ab, Server bindet danach neu (`NL_REMOTE listen …`) | ungeprüft |

## Dolphin (2026-11-26, grün)

- `--marker NL_READY --marker "NL_WIFI ok"`: `NL_WIFI ok gw=yes gw_ms=2 reconnects=0`.
- `--cover-server --tls --debug clear_covers=1 --marker NL_COVER_DL_DONE --marker "NL_WIFI ok"`: grün.
