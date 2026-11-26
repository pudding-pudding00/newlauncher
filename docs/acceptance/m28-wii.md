# Hardware-Abnahme M28: OneDrive, Anbieterwechsel, Alle abgleichen, Abmelden (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher **mit TLS**, WLAN, eigenes Microsoft-Konto und eigenes
Google-Konto, Handy oder PC; Dolphin am PC mit denselben Konten als zweites Gerät).
Spec: `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`, Plan
`docs/superpowers/plans/2026-11-26-m28-onedrive.md`, Einrichtung `docs/cloud-setup.md`.

**Vorher:** App-Registrierungen nach `docs/cloud-setup.md` (Microsoft Entra: persönliche Konten, „Öffentliche
Clientflows zulassen“ = Ja). Vor jedem Kopieren auf die SD-Karte fragen. Ein Pull in den echten NAND schreibt wie
M26 in `/title/…/data` (mit Sicherung vorher); vor dem ersten Mal fragen und ein BootMii-NAND-Backup empfehlen. Für
O2–S3 zuerst Emulator- oder Nintendont-Spiele nehmen.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| O1 | `config.ini` mit `onedrive_client_id`, Anbieter OneDrive → Anmelden | Szene zeigt `microsoft.com/link`, Code und Restzeit; nach Eingabe am Handy „Angemeldet“; Log `NL_CLOUD_LINKED onedrive` | ungeprüft |
| O2 | Spiel A spielen und speichern, zurück in newlauncher | Log `NL_CLOUD_UPLOAD <einheit> ok`; im OneDrive unter Apps → <App-Name> liegt `<einheit>.zip` mit Beschreibung `nl1 hash=…` | ungeprüft |
| O3 | In Dolphin (OneDrive) Spiel A weiterspielen, dann auf der Wii starten | `NL_CLOUD <einheit> pull ok`, Spiel lädt den Dolphin-Stand, Sicherung vorher angelegt | ungeprüft |
| O4 | Auf Wii und Dolphin unabhängig speichern, auf der Wii starten | Konfliktdialog wie M27 C3, Gerätename aus der Beschreibung | ungeprüft |
| O5 | Wii > 1 h laufen lassen, Spiel starten | `NL_CLOUD_REFRESH ok`, Abgleich klappt | ungeprüft |
| O6 | Log nach O1–O5 durchsuchen | Kein Token, kein Gerätecode, keine Datei-ID, keine Download-Adresse, keine E-Mail | ungeprüft |
| S1 | Angemeldet bei Google Drive mit Ständen für A und B; Anbieter auf OneDrive umstellen, Anmelden | Google-Token widerrufen (Log `NL_CLOUD_UNLINKED revoke=ok`); danach Abgleich aller Spiele, Log `NL_CLOUD_SWITCH uploaded=N conflict=0`; im OneDrive liegen A und B | ungeprüft |
| S2 | Wie S1, aber OneDrive hat für A schon einen anderen Stand | Dialog einmal für alle; „Wii-Stände nehmen“ → A wird hochgeladen (`NL_CLOUD_SWITCH_CHOICE wii`) | ungeprüft |
| S3 | Wie S2 mit „Beim Spielstart fragen“ | Nichts überschrieben; beim Start von A Konfliktdialog | ungeprüft |
| Y1 | Cloud-Menü → Alle abgleichen (ein Spiel nur in der Cloud, eins nur lokal) | Status „Abgleich n/m …“, am Ende geholt 1 / hochgeladen 1; Log `NL_CLOUD_SYNCALL pulled=1 pushed=1` | ungeprüft |
| Y2 | Alle abgleichen, während des Laufs „Abgleich abbrechen“ | Lauf endet nach dem aktuellen Spiel, kein halber Spielstand | ungeprüft |
| Y3 | WLAN aus, Alle abgleichen | Log `NL_CLOUD_SYNCALL offline`, nichts verändert | ungeprüft |
| U1 | Google Drive: Abmelden | Log `NL_CLOUD_UNLINKED revoke=ok`; App unter myaccount.google.com/permissions verschwunden; `tokens.bin` weg | ungeprüft |
| U2 | OneDrive: Abmelden | Log `NL_CLOUD_UNLINKED revoke=none`; Tokens weg; Hinweis auf account.live.com/consent/Manage in der Anleitung | ungeprüft |
| U3 | Abmelden bei WLAN aus (Google) | Tokens trotzdem weg, Meldung „Widerruf nicht bestätigt“, Log `revoke=fail` | ungeprüft |
| L1 | Sprache Englisch | Anbieterwahl, Wechsel-Dialog, „Alle abgleichen“ und Status englisch | ungeprüft |

## Dolphin (2026-11-26)

`python tools/dolphin_smoke.py --cloud-fake drive|onedrive --cloud-step S` gegen `tools/cloud_fake.py` (HTTPS mit
Test-CA; der Graph-Fake leitet Downloads per 302 auf `/dl/<id>` um und lehnt dort ein mitgeschicktes
`Authorization` ab) → Ergebnisse siehe unten.

- **link, push, pull, conflict, refresh, offline:** wie M27, jetzt für beide Anbieter. Smokes melden sich ohne
  `cloud_sync` an; nach der Anmeldung läuft deshalb immer der Wechsel-Abgleich über alle Spiele.
- **syncall:** GB nur lokal (A), Galaxy-NAND nur in der Cloud (B) → `NL_CLOUD wii-RMGP pull ok`,
  `NL_CLOUD <gb> push ok`, `NL_CLOUD_SWITCH uploaded=1 conflict=0`; NAND-Datei hat B, Fake hat A.
- **switch:** lokal A, Cloud B, `cloud_switch = wii` → `NL_CLOUD <einheit> conflict`, `NL_CLOUD_SWITCH_CHOICE wii`,
  `NL_CLOUD_SWITCH uploaded=1 conflict=0`, Fake hat A.
- **unlink:** Drive → `NL_CLOUD_UNLINKED revoke=ok`, genau ein Widerruf, Fake danach abgemeldet; OneDrive →
  `revoke=none`, kein Widerruf.
- Alle Schritte: kein Token, kein Gerätecode, kein Client-Secret im Log.
