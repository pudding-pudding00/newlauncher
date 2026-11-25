# Hardware-Abnahme M21: Rückkehr aus Wii-Spielen, Cover automatisch (nur Wii / vWii)

Status: **ungeprüft** (erfordert eine echte Wii mit HBC, newlauncher auf der SD-Karte, cIOS 249 = d2x,
USB-Platte an Port 0 und Netzwerk). Spec: `docs/superpowers/specs/2026-11-25-m21-rueckkehr-cover-design.md`.

## Vorbereitung

- newlauncher als `sd:/apps/newlauncher/boot.dol` auf einer FAT32-SD-Karte. Der Rückkehr-Stub lädt genau diese
  Datei. Ein Start per wiiload (RAM) installiert keinen Stub.
- Ein Wii-Spiel auf USB (z. B. `RUUE01`), ein GC-Spiel für Nintendont, für G4 ein Kanal im EmuNAND.
- Für C1: WLAN eingerichtet, einige Wii- und GC-Spiele ohne Cover (z. B. `covers_src` und `.tex` gelöscht).
- Log: `sd:/newlauncher/log.txt` bzw. `python tools/wii_remote.py --host 192.0.2.10 log`.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| G1 | Wii-Spiel (`RUUE01`) aus newlauncher starten, HOME → „Wii-Menü“ | newlauncher erscheint (nicht das Systemmenü); Log: `NL_RETURN_STUB …` vor dem Start, danach `NL_FROM_GAME ios=249->58 unblock=0`; USB-Spiele sind sichtbar | ungeprüft |
| G2 | G1 dreimal hintereinander | Jede Rückkehr klappt; kein Hänger, kein Systemmenü | ungeprüft |
| G3 | Wii-Spiel mit aktiven Cheats starten, HOME → „Wii-Menü“ | Log `NL_RETURN_STUB skip cheats`; Rückkehr ins Systemmenü (bzw. `return_to`) wie bisher | ungeprüft |
| G4 | EmuNAND-Kanal aus newlauncher starten, HOME → „Wii-Menü“ | newlauncher erscheint; Log `NL_FROM_GAME` | ungeprüft |
| G5 | Nach G1 ein GC-Spiel (Nintendont) starten | Startet. Sonst: AHBPROT nach `IOS_ReloadIOS(58)` wieder an, als Folgepunkt notieren | ungeprüft |
| C1 | newlauncher starten, keine Aktion | Nach dem Scan und sobald das WLAN verbunden ist: Log `NL_COVER_AUTO jobs=<n>`; Cover für Wii und GC erscheinen nach und nach. Mit „Cover automatisch laden = Aus“ kein `NL_COVER_AUTO` | ungeprüft |

## Bekannte Grenzen

- NAND-Kanäle (z. B. Virtual Console auf dem echten NAND) starten ohne Booter direkt über `ES_LaunchTitle`. Dort
  greift der Patch nicht: „Wii-Menü“ führt weiter ins Systemmenü.
- Mit Cheats kein Stub (der Codehandler belegt denselben Speicher bei `0x80001800`).
- Findet `PatchReturnTo` das Muster in `__OSLaunchMenu` nicht (seltene SDKs), bleibt das Systemmenü.
- Schlägt der Stub fehl, startet die Konsole neu ins Systemmenü; der Fortschrittscode erscheint beim nächsten Start
  als `NL_RETURN_STUB present stage=N`.
