# Hardware-Abnahme M20: Direktstart, Rückkehr, Fernsteuerung, Selbst-Update (nur Wii / vWii)

Status: **ungeprüft** (erfordert eine echte Wii mit HBC, newlauncher auf der SD-Karte und Netzwerk).

## Vorbereitung

- newlauncher als `sd:/apps/newlauncher/boot.dol` auf einer FAT32-SD-Karte (der Rückkehr-Stub lädt genau diese
  Datei über ihre FAT-Extents nach).
- Mindestens ein Emulator (z. B. Not64, GBC) und eine eigene ROM auf SD oder USB.
- PC im selben Netz: `tools/wii_remote.py` bzw. wiiload (HBC, Port 4299).

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| R1 | ROM-Eintrag (N64, GBC …) in newlauncher starten | Emulator startet direkt mit der ROM, ohne eigenes Menü | ungeprüft |
| R2 | Im Emulator „Exit“ bzw. „Zurück zum Loader“ wählen | newlauncher erscheint wieder (nicht der HBC); Log enthält `NL_RETURN_STUB present stage=0` | ungeprüft |
| R3 | R1/R2 dreimal hintereinander | Jede Rückkehr klappt; kein Absturz, kein Systemmenü-Neustart | ungeprüft |
| R4 | Nach R2: HOME → Homebrew Channel | HBC startet (als Titel); newlauncher lädt sich nicht erneut | ungeprüft |
| R5 | newlauncher per wiiload (RAM, nicht SD) starten, dann einen Emulator starten und beenden | Kein Stub (`NL_RETURN_STUB skip self=…`); Rückkehr in den HBC wie bisher | ungeprüft |
| R6 | Wii-Spiel oder Kanal starten | Kein Stub; Spiel/Codehandler laufen unverändert | ungeprüft |
| F1 | Fernstart: `wii_remote.py` startet einen Eintrag, liest Log und Zustand | Eintrag startet; Antworten kommen vollständig an | ungeprüft |
| U1 | Einstellungen → Update aus GitHub-Releases, danach Neustart aus newlauncher | Neue Version läuft nach dem Neustart; Versionsanzeige stimmt | ungeprüft |
| U2 | Update per Netzwerk über den HBC (wiiload) | HBC nimmt die DOL an, newlauncher startet | ungeprüft |

Fällt der Stub aus (z. B. Leserfehler), bleibt bei 0x80002FF0 ein Fortschrittscode stehen und die Konsole startet
neu ins Systemmenü. Übersteht MEM1 den Neustart, erscheint er beim nächsten Start von newlauncher im Log als
`NL_RETURN_STUB present stage=N`.
