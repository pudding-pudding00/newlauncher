# M21: Rückkehr aus Wii-Spielen in newlauncher, Cover automatisch laden (Kurz-Spec)

Stand: 2026-11-25. Ergänzt Spec v2 (`2026-11-11-v2-universell-design.md`) und M20
(`2026-11-25-m20-direktstart-design.md`).

## Teil A: „Wii-Menü“ im Spiel führt zurück in newlauncher

### Absicht

Aus allem, was newlauncher startet, kommt man nur in newlauncher zurück. Für Emulatoren gilt das seit M20
(Rückkehr-Stub). Für Wii-Spiele und Kanäle soll „Wii-Menü“ im HOME-Menü (und jeder andere Weg des Spiels über
`__OSLaunchMenu`) newlauncher statt des Systemmenüs starten.

### Ziele / Nicht-Ziele

- Ziel: Wii-Spiel (USB, Disc) und Kanal, von newlauncher gestartet → „Wii-Menü“ → newlauncher.
- Nicht-Ziel: Forwarder-Kanal auf dem NAND (bleibt Nicht-Ziel aus Spec v1/v2). Kein Schreiben auf den NAND.
- Nicht-Ziel: „Zurücksetzen“ im HOME-Menü (Neustart des Spiels) und Ausschalten bleiben unverändert.
- Anti-Ziel: Ein Fehler darf keinen Hänger erzeugen. Schlägt die Rückkehr fehl, startet die Konsole wie bisher
  ins Systemmenü neu.

### Ansätze

| Ansatz | Bewertung |
|---|---|
| **A: Spiel-Patch + Rückkehr-Stub** (gewählt) | Der Booter biegt den `ES_LaunchTitle`-Aufruf in `__OSLaunchMenu` auf den M20-Stub bei `0x80001800` um. Kein NAND, alles im Projekt. Grenze: nicht zusammen mit Cheats (Codehandler belegt denselben Speicher). |
| B: Forwarder-Kanal | Robust, aber NAND-Installation (Nicht-Ziel) und WAD-Bau braucht den Common Key. |
| C: Priiloader | Nur mit installiertem Priiloader, nicht allgemein. |

Der Nutzer hat A gewählt.

### Entwurf

1. **Launcher (`src/app/main.cpp`, Wii-Pfad):** Vor `unmountAll()` den Rückkehr-Stub installieren wie beim
   Emulator, mit Argument `--from-game` (zusätzlich `--remote`, wenn die Fernsteuerung lief). Nicht installiert
   wird er, wenn Cheats aktiv sind (GCT nicht leer) oder newlauncher nicht von `sd:/` läuft (wie M20 R5). Die
   Entscheidung „Stub ja/nein“ liegt als reine Funktion in `core/` (Host-Test).
2. **Booter (`boot/source`):**
   - Erkennt den Stub selbst: `hooktype == 0` und `nlrs_params_ok(0x80002800)` (bindet `stub_check.h` ein).
   - Dann patcht `PatchReturnTo` statt der Title-ID einen Sprung zum Stub: an `ad[2]` (Aufruf von
     `ES_LaunchTitle`) vier Befehle `lis r3,0x8000; ori r3,r3,0x1800; mtctr r3; bctr`. Die beiden
     `ES_GetTicketViews`-Aufrufe bleiben unverändert. Gilt für Spiele (Apploader) und Kanäle.
   - Der Stub gewinnt gegen `returnTo`. Ohne Stub bleibt `returnTo` wie bisher.
   - Die Disc-ID für WiiRD (`memcpy` nach `0x80001800`) entfällt, wenn der Stub da ist (ohne Codehandler
     braucht sie niemand).
3. **Stub (`src/launch/return_stub/stub.c`):** Schaltet beim Einsprung die externen Interrupts ab (MSR[EE]),
   damit der IPC-Treiber des Spiels keine Antworten abfängt. Sonst unverändert.
4. **newlauncher beim Start mit `--from-game`:** Läuft dann noch unter cIOS 249 mit aktiver Reload-Sperre von
   d2x. Zuerst die Sperre aufheben (`/dev/es` ioctlv `0xA0`, Modus 0; Fehler werden geloggt, nicht fatal), dann
   `IOS_ReloadIOS(58)`. Log `NL_FROM_GAME ios=<vorher>→<nachher> unblock=<ret>`. Das steht ganz am Anfang von
   `main`, vor USB, Netz und Video.

### Bekannte Grenzen

- Mit Cheats: kein Stub, Rückkehr wie bisher (Systemmenü bzw. `return_to`).
- Findet `PatchReturnTo` das Muster nicht (seltene SDKs), bleibt das Systemmenü.
- Nach `IOS_ReloadIOS(58)` ist AHBPROT vermutlich wieder an: GC-Spiele (Nintendont) verlangen dann einen Start
  über den HBC. Prüfpunkt auf der Wii; ggf. eigener Folgepunkt.

### Tests

- Host: Entscheidung „Stub installieren“ (Cheats, Pfad, Spieltyp); Erkennung von `--from-game` in den Startargumenten.
- Booter: Wii-Build mit 0 Warnungen; kein Host-Test (Patch auf Spielcode).
- Hardware (`docs/acceptance/m21-wii.md`): RUUE01 → HOME → Wii-Menü → newlauncher (Log `NL_FROM_GAME`,
  USB-Spiele sichtbar); dreimal hintereinander; mit Cheats → Systemmenü; Kanal → Wii-Menü → newlauncher;
  GC-Start nach der Rückkehr (AHBPROT).

## Teil B: Cover für Wii und GC automatisch laden

### Absicht

Cover sollen ohne Zutun erscheinen. Bisher lädt sie nur die Aktion „Fehlende Cover laden“ (Spec v1 6.4).

### Entwurf

- Neue Einstellung **„Cover automatisch laden / Download covers automatically“**, `[covers] auto = 1`
  (Standard an).
- Ist sie an: Sobald der Bibliotheks-Scan fertig ist und das Netz bereit ist, startet newlauncher einmal pro
  Sitzung denselben Ablauf wie „Fehlende Cover laden“, aber still (kein Dialog „Keine fehlenden Cover“). Neue
  Cover erscheinen wie heute nach und nach. Log `NL_COVER_AUTO jobs=<n>`.
- Die Aktion „Fehlende Cover laden“ bleibt.
- Läuft gerade ein anderer Netz-Auftrag, wird der automatische Lauf nach dessen Ende nachgeholt.

### Tests

- Host: Config-Parsing/Serialisierung von `auto`; Einstellungszeile; Entscheidung „jetzt automatisch starten“
  (Scan fertig, Netz bereit, noch nicht gelaufen, Einstellung an).
- Dolphin-Smoke: mit `--cover-server` ohne `[debug] fetch_covers` erscheint `NL_COVER_AUTO` und `NL_COVER_DL … ok`.
- Hardware: nach dem Start erscheinen Cover für Wii und GC ohne Aktion.

## Dem Umsetzer überlassen

Namen der Funktionen, genaue Position der Prüfung im Startablauf, Wortlaut der Log-Zeilen außer den genannten.
