# Hardware-Abnahme M29: Dropbox, Kopplungsseite, QR, Tastatur (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher **mit TLS**, WLAN, eigenes Dropbox-Konto, Handy im selben
WLAN; Dolphin am PC mit demselben Konto als zweites Gerät).
Spec: `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`, Plan
`docs/superpowers/plans/2026-11-26-m29-dropbox.md`, Einrichtung `docs/cloud-setup.md`.

**Vorher:** Dropbox-App nach `docs/cloud-setup.md` (Scoped access, App folder, Rechte `files.content.read`,
`files.content.write`, `account_info.read`, „Allow public clients“). Vor jedem Kopieren auf die SD-Karte fragen. Ein
Pull in den echten NAND schreibt wie M26 in `/title/…/data` (mit Sicherung vorher); vor dem ersten Mal fragen und
ein BootMii-NAND-Backup empfehlen. Für D2–D4 zuerst Emulator- oder Nintendont-Spiele nehmen.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| P1 | `config.ini` mit `dropbox_app_key`, Anbieter Dropbox → Anmelden | Szene mit QR-Code und `http://<wii-ip>:4301/<token>`, Restzeit läuft; Log `NL_PAIR listen 4301` | ungeprüft |
| P2 | QR mit dem Handy scannen, „Bei Dropbox zulassen“, Code auf der Seite senden | Handy zeigt „Code übermittelt. Weiter auf der Wii.“; Wii „Angemeldet“; Log `NL_PAIR ok`, `NL_CLOUD_LINKED dropbox`, `NL_PAIR stop`; Seite danach nicht mehr erreichbar | ungeprüft |
| P3 | Seite nach der Einsendung neu laden bzw. ein zweites Mal senden | „Diese Seite ist nicht mehr aktiv“; kein zweiter Code-Tausch | ungeprüft |
| P4 | Anmelden, Szene 5 Minuten offen lassen | Szene meldet Ablauf, Seite antwortet „nicht mehr aktiv“; Log `NL_CLOUD_LINK expired` | ungeprüft |
| K1 | Anmelden, **Y** → direkter QR, mit dem Handy über Mobilfunk freigeben, **A** → Code mit der Bildschirmtastatur eingeben | Ebenen abc/ABC/123/# erreichbar, Code bis 128 Zeichen; „Angemeldet“ | ungeprüft |
| K2 | Code absichtlich falsch eingeben (Tastatur oder Seite) | „Code abgelehnt, bitte erneut eingeben“, Log `NL_CLOUD_LINK badcode`; richtiger Code danach klappt (Seite nimmt ihn an) | ungeprüft |
| K3 | Handy im Gast-WLAN bzw. ohne WLAN | Seite unerreichbar; Weg über Y + A klappt (wie K1) | ungeprüft |
| D1 | Spiel A spielen und speichern, zurück in newlauncher | Log `NL_CLOUD_UPLOAD <einheit> ok`; in der Dropbox unter Apps → <App-Name> liegt `<einheit>.zip` | ungeprüft |
| D2 | In Dolphin (Dropbox) Spiel A weiterspielen, dann auf der Wii starten | `NL_CLOUD <einheit> pull ok`, Spiel lädt den Dolphin-Stand, Sicherung vorher angelegt | ungeprüft |
| D3 | Auf Wii und Dolphin unabhängig speichern, auf der Wii starten | Konfliktdialog mit Gerätename und Zeit aus dem ZIP (Dropbox hat kein Beschreibungsfeld) | ungeprüft |
| D4 | Wii > 4 h laufen lassen, Spiel starten | `NL_CLOUD_REFRESH ok`, Abgleich klappt | ungeprüft |
| D5 | Abmelden | Log `NL_CLOUD_UNLINKED revoke=ok`; App unter dropbox.com → Einstellungen → Verbundene Apps verschwunden; Tokens auf der SD weg | ungeprüft |
| D6 | Log nach P1–D5 durchsuchen | Kein Token, kein Code, kein Kopplungs-Token, keine Datei-ID, keine E-Mail | ungeprüft |
| L1 | Sprache Englisch | Kopplungsszene, Kopplungsseite am Handy, Tastatur-Hinweis und Meldungen englisch | ungeprüft |

## Dolphin (2026-11-26)

`python tools/dolphin_smoke.py --cloud-fake dropbox --cloud-step S` gegen `tools/cloud_fake.py` (HTTPS mit Test-CA;
PKCE mit S256, Code nur einmal gültig, 409 `path/conflict` bei falscher Rev). `tools/cloud_smoke.py` spielt das Handy
über `127.0.0.1:4301` (Dolphin reicht die Wii-Sockets an den PC durch).

- **link:** `NL_PAIR listen 4301`, `NL_PAIR ok`, `NL_CLOUD_LINKED dropbox`, `NL_PAIR stop`.
- **badcode:** erst `WRONG-code_0` → `NL_CLOUD_LINK badcode`, Seite wieder scharf, richtiger Code → angemeldet.
- **pairtwice:** zweite Einsendung → 410, Log `NL_PAIR reject`, genau ein Code-Tausch.
- **push, pull, conflict, refresh, offline, syncall, switch, unlink:** wie M28, jetzt auch mit Dropbox; Hash, Gerät
  und Zeit kommen aus dem ZIP; `unlink` → `revoke=ok`, genau ein Widerruf.
- Alle Schritte: kein Token, kein Code, kein Kopplungs-Token im Log.
