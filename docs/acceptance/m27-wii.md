# Hardware-Abnahme M27: Cloud-Kern + Google Drive (nur Wii / vWii)

Status: **ungeprüft** (echte Wii mit HBC, newlauncher **mit TLS**, WLAN, eigenes Google-Konto, Handy oder PC;
Dolphin am PC mit demselben Konto als zweites Gerät).
Spec: `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md`, Plan
`docs/superpowers/plans/2026-11-26-m27-cloud-drive.md`, Einrichtung `docs/cloud-setup.md`.

**Vorher:** App-Registrierung nach `docs/cloud-setup.md` (Status „In Produktion“). Vor jedem Kopieren auf die
SD-Karte fragen. Ein Pull in den echten NAND schreibt wie M26 in `/title/…/data` (mit Sicherung vorher); vor dem
ersten Mal fragen und ein BootMii-NAND-Backup empfehlen. Für C1–C5 zuerst ein Emulator- oder Nintendont-Spiel nehmen.

## Prüfpunkte

| Punkt | Prüfung | Erwartetes Ergebnis | Ergebnis |
|---|---|---|---|
| A1 | `config.ini` ohne `drive_client_id`, Einstellungen → Cloud-Spielstände | Google Drive ausgegraut, Hinweis auf `docs/cloud-setup.md` | ungeprüft |
| A2 | Mit Client-ID: Anbieter Google Drive → Anmelden | Szene zeigt Adresse, Code und Restzeit; nach Eingabe am Handy „Angemeldet“; Log `NL_CLOUD_LINKED drive` | ungeprüft |
| A3 | Anmelden abbrechen (B) bzw. am Handy „Ablehnen“ | Szene schließt bzw. meldet Ablehnung, kein `tokens.bin` | ungeprüft |
| A4 | Log nach A2 durchsuchen (`sd:/newlauncher/log.txt` bzw. Remote-Log) | Kein Access-/Refresh-Token, kein Gerätecode, kein Client-Secret, keine E-Mail | ungeprüft |
| C1 | Spiel A starten (Cloud leer), spielen, speichern, zurück in newlauncher | Beim nächsten Start Hintergrund-Upload: Log `NL_CLOUD_UPLOAD <einheit> ok`; in Drive liegt `newlauncher/<einheit>.zip` | ungeprüft |
| C2 | In Dolphin mit demselben Konto Spiel A weiterspielen und speichern (Upload), dann auf der Wii Spiel A starten | Kurzer Abgleich-Bildschirm, dann `NL_CLOUD <einheit> pull ok`; das Spiel lädt den Stand aus Dolphin; vorher wurde eine lokale Sicherung angelegt | ungeprüft |
| C3 | Auf Wii **und** Dolphin unabhängig speichern, dann auf der Wii starten | Konfliktdialog mit Sitzungszeit (Wii) und Gerätename/Serverzeit (Cloud); „Cloud-Stand laden“ → Cloud-Stand im Spiel | ungeprüft |
| C4 | Wie C3, aber „Wii-Stand behalten“ | Upload des Wii-Stands, Dolphin bekommt ihn beim nächsten Start | ungeprüft |
| C5 | Abgleich-Bildschirm mit B verlassen | Spiel startet sofort ohne Abgleich | ungeprüft |
| N1 | WLAN am Router aus, Spiel starten | Kein Hänger: Log `NL_CLOUD <einheit> skip offline`, Spiel startet; nach dem Spielen bleibt der Eintrag in `pending.ini` | ungeprüft |
| N2 | WLAN wieder an, newlauncher neu starten | Liegengebliebener Upload läuft durch, `pending.ini` leer | ungeprüft |
| R1 | Wii > 1 h laufen lassen oder neu starten nach > 1 h, Spiel starten | Abgleich klappt; Log `NL_CLOUD_REFRESH ok` (Access-Token erneuert) | ungeprüft |
| R2 | Nach > 7 Tagen (App „In Produktion“) Spiel starten | Abgleich ohne neue Anmeldung | ungeprüft |
| R3 | Zugriff unter myaccount.google.com/permissions entziehen, Spiel starten | Spiel startet ohne Abgleich (Log `NL_CLOUD <einheit> skip auth`); Einstellungen → Cloud-Spielstände zeigt „Anmeldung abgelaufen – bitte neu anmelden“ | ungeprüft |
| W1 | Wii-Spiel (echter NAND, nach Rückfrage und BootMii-Backup) über C1/C2 | Upload und Pull wie bei C1/C2, Spiel lädt den Stand **und speichert danach selbst** | ungeprüft |
| U1 | Abmelden | `tokens.bin` und Konto-Einträge in `state.ini` weg, Status „Nicht angemeldet“ | ungeprüft |
| L1 | Sprache Englisch | Alle Texte der Cloud-Szenen und des Konfliktdialogs englisch | ungeprüft |

## Dolphin (2026-11-26, grün)

`python tools/dolphin_smoke.py --cloud-fake drive --cloud-step S` gegen `tools/cloud_fake.py` (HTTPS mit Test-CA,
Gerätecode wird sofort bestätigt) → `SMOKE OK` für jedes S:

- **link:** `NL_CLOUD_LINKED drive`.
- **push:** `session.ini` liegt da → `NL_CLOUD_UPLOAD <einheit> ok`; ZIP im Fake hat Einheit, Inhalts-Hash und
  `appProperties` des GB-Spielstands.
- **pull:** Cloud hat B, lokal nichts → `NL_CLOUD <einheit> pull ok`, Datei im SD-Abbild hat die Bytes B.
- **conflict:** lokal A, Cloud B, keine Basis, `cloud_conflict = cloud` → `NL_CLOUD_CONFLICT`, `NL_CLOUD_CHOICE … cloud`,
  Pull, Datei hat B.
- **refresh:** erste Datei-Anfrage bekommt 401 → `NL_CLOUD_REFRESH ok`, Upload klappt, genau ein Refresh.
- **offline:** Fake schließt nach der Anmeldung den Listener → `NL_CLOUD <einheit> skip offline`, kein Upload.
- Alle Schritte: kein Token, kein Gerätecode, kein Client-Secret im Log; ohne 401 kein Refresh (Tokens bleiben je
  Sitzung im Speicher).
- Build ohne TLS: `--dol build/wii-notls/newlauncher.dol --marker "NL_CLOUD off notls"` → `SMOKE OK`.
