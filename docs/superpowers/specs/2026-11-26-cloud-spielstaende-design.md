# M26–M29: Spielstände und Cloud-Abgleich wie bei Steam

Stand: 2026-11-26. Vorarbeit: `docs/superpowers/spikes/m26-cloud-spielstaende.md`.

## Absicht (Wünsche des Nutzers)

1. Ein **eingebauter Save-Manager**: Spielstände sichern und wiederherstellen.
2. **Cloud-Spielstände wie bei Steam:** vor dem Spielstart den neueren Stand aus der Cloud holen, nach dem Spielen
   Änderungen hochladen, bei Konflikt fragen.
3. Anbieter **Google Drive, OneDrive, Dropbox**. iCloud ist gestrichen (keine API für Nicht-Apple-Geräte).
   MEGA hat der Nutzer am 2026-11-26 gestrichen.
4. „Setze alles um was nötig ist.“

Entscheidungen des Nutzers (bindend):

- **NAND:** Wiederherstellen darf in den echten NAND schreiben, aber **nur** in `/title/<hi>/<lo>/data`, über einen
  ISFS-Rechte-Patch im Speicher und **immer mit lokaler Sicherung vorher**.
- **Genau ein Anbieter aktiv.** Beim Wechsel wird alles zum neuen hochgeladen.
- **Eingabe** des Dropbox-Codes über eine **Kopplungsseite auf dem Handy** (QR-Code am TV) und als
  Ausweichweg über die **erweiterte Bildschirmtastatur**.

## Änderung früherer Nicht-Ziele

- M19 („schreibt nie in den EmuNAND-Ordner“) und M21 („kein Schreiben auf den NAND“) gelten weiter, **außer** für
  Spielstand-Ordner `title/<hi>/<lo>/data` (echter NAND und EmuNAND) beim Wiederherstellen nach Ziel 3.
- Alles andere im NAND (Tickets, TMDs, Inhalte, `/shared2`, SYSCONF) bleibt tabu.

## Ziele

### 1. Spielstand-Einheiten (M26)

Eine **Einheit** ist der Satz Dateien, der zu einem Spiel gehört. ID und Ort:

| Quelle | Einheiten-ID | Ort |
|---|---|---|
| Wii-Spiel, `save=real` | `wii-<ID4>` | NAND `/title/00010000/<hex(ID4)>/data/*`, sonst `00010004` |
| Wii-Spiel, `save=emu` | `wii-<ID4>` | `<emunand>/title/00010000/<hex>/data/*` |
| EmuNAND-Kanal | `ch-<tid16>` | `<emunand>/title/<hi>/<lo>/data/*` |
| echter NAND-Kanal (VC) | `ch-<tid16>` | NAND `/title/<hi>/<lo>/data/*` |
| GameCube (Nintendont, je Spiel) | `gc-<ID4>` | `<spielgerät>:/saves/<ID4>.raw` |
| GameCube, `memcard=multi` | `gc-ninmem` bzw. `gc-ninmemj` | `<spielgerät>:/saves/ninmem.raw` bzw. `ninmemj.raw` (JP/KOR) |
| ROM (Emulator) | `<konsole>-<slug>-<crc8>` | Speicherordner des Profils (siehe unten) |

- Die gleiche Einheit hat auf echter Wii, EmuNAND und Dolphin dieselbe ID: Ein Spielstand wandert zwischen ihnen.
- `slug` = Dateiname ohne Endung, nur `[a-z0-9-]`, höchstens 40 Zeichen; `crc8` = 8 Hex-Ziffern CRC-32 des
  Spielschlüssels (Eindeutigkeit trotz Kürzung).
- **Emulator-Speicherorte** (Spike S4): fceugx `fceugx/saves/<stem>.sav`, snes9xgx `snes9xgx/saves/<stem>.srm`
  und `.rtc`, vbagx `vbagx/saves/<stem>.sav`. Gerät: vorhandener Ordner auf `sd:` vor `usb:`; `saveFolder` aus
  `apps/<app>/settings.xml` überschreibt den Ordner. Savestates (`.frz`, `.fcs`, `.sgm`, `.st0`–`.st9`, `.state*`)
  und Bilder (`.png`) gehören nie dazu.
- Für not64, wii64 und RetroArch ist der Ort ungeklärt: `emulators.ini [<konsolen-abschnitt>] save_dir = …`, `save_ext = …`
  (Liste; Abschnitte `nes`, `snes`, `gb`, `gbc`, `gba`, `n64_not64`, `n64_wii64`). Ohne Eintrag gibt es für diese Profile **keine** Einheit (Anzeige „Speicherort unbekannt“).
- `memcard=real` (echte Speicherkarte), Savestates und Dateien außerhalb der Tabelle sind nicht im Umfang.
- Eine Einheit ist höchstens **16 MiB** roh groß (Nintendont-Multikarte).

### 2. Paket-Format (M26)

- Ein **ZIP** je Einheit (Deflate oder gespeichert), Dateien unter ihren relativen Pfaden zum Einheiten-Stamm
  (Wii/Kanal: wie in Dolphins `Wii/title/<hi>/<lo>/data/`). Kein `data.bin`, kein Nintendo-Schlüssel.
- Erster Eintrag `newlauncher-save.ini`: `format = 1`, `unit`, `title`, `device` (Gerätename), `saved` (Unix-Zeit),
  `hash`, `files`.
- **Inhalts-Hash:** SHA-256 über die nach Pfad sortierten Zeilen `<pfad>\n<größe>\n<sha256 hex>\n`. Die ZIP-Bytes
  zählen nie (Deflate darf sich ändern).
- Beim Auspacken: nur sichere relative Pfade (`isSafeZipPath`), Hash muss passen, Größengrenze.

### 3. Lokale Sicherung und Wiederherstellung (M26)

- **Sicherung:** `<data>/saves/<einheit>/<yyyymmdd-hhmmss>.zip`, höchstens **5** je Einheit (älteste fällt weg).
- **Wiederherstellung** aus einer Sicherung oder der Cloud:
  1. aktuelle Dateien als Sicherung ablegen (immer, auch wenn leer: dann ohne ZIP),
  2. Journal `<data>/saves/restore.ini` (Einheit, Sicherung, Phase) schreiben,
  3. jede Datei atomar schreiben (FAT: `writeAtomic`; NAND: Datei unter `/tmp` anlegen, dann `ISFS_Rename`;
     Eigentümer, Gruppe und Rechte der bisherigen Datei bzw. des Ordners übernehmen),
  4. zurücklesen und Hash prüfen, 5. Journal löschen.
- Findet der Start ein Journal, spielt er die Sicherung daraus zurück (`NL_SAVE_RECOVER rollback`).
- Dateien, die es im Paket nicht gibt, bleiben liegen (**Löschungen werden nie übertragen**).
- **NAND-Zugriff:** Vor jedem NAND-Zugriff `applyIsfsPermissionPatch()`. Ergebnis `NotFound`/`NoAhbprot` → NAND-
  Einheiten sind gesperrt (Anzeige „NAND gesperrt“), alles andere geht weiter.

### 4. Save-Manager-Oberfläche (M26)

- Details-Seite: neue Zeile **„Spielstand“** (unter „Cheats“) → Szene **Spielstand**:
  Einheit, Ort, Größe, Zeit des lokalen Stands, Liste der Sicherungen; Knöpfe **Sichern**, **Wiederherstellen**
  (Auswahl der Sicherung), **Sicherung löschen**. Bei Cloud-Anbieter zusätzlich Cloud-Stand und **Jetzt abgleichen**.
- Wiederherstellen fragt über einen **Auswahldialog** (Ja/Nein, neu: `ChoiceDialog` mit 2–3 Knöpfen).
- Texte immer Deutsch und Englisch.

### 5. Cloud-Abgleich wie bei Steam (M27)

**Vor dem Start** (nach PIN, Preflight und Hinweisen, vor dem Festschreiben des Starts):

1. Einheit zum aufgelösten Startziel bestimmen. Keine Einheit oder kein Anbieter → normal starten.
2. Szene „Spielstand wird abgeglichen …“ mit Fortschritt; **B = Ohne Abgleich spielen**.
3. Cloud-Stand abfragen (Auftrag vorne in der Netz-Warteschlange, Zeitlimit 20 s). Offline oder Fehler → Hinweis
   und Start ohne Abgleich (`NL_CLOUD_SKIP <grund>`).
4. Entscheidung nach Tabelle, ggf. herunterladen und nach Ziel 3 wiederherstellen.
5. Konflikt → Dialog „**Wii-Stand behalten** (lädt hoch) / **Cloud-Stand laden** / Abbrechen“ mit Zeit und
   Gerätename je Seite.

**Nach dem Spielen:** Vor dem Start schreibt der Launcher `<data>/cloud/session.ini` (Einheit, Zeit). Beim nächsten
Start des Launchers (Rückkehr, Ende einer DOL, Kaltstart) wandert der Eintrag nach `<data>/cloud/pending.ini`;
der Hintergrund-Upload lädt geänderte Einheiten hoch. Ein Eintrag fällt erst nach bestätigtem Upload heraus.
NAND gesperrt (z. B. nach einem Wii-Spiel) → Eintrag bleibt bis zum nächsten Start mit NAND-Zugriff.

**Entscheidungstabelle** (Basis = letzter gemeinsamer Stand aus `<data>/cloud/state.ini`):

| lokal ggü. Basis | Cloud ggü. Basis | Aktion |
|---|---|---|
| gleich | gleich | nichts |
| gleich | neu | herunterladen, wenn Cloud-Hash ≠ lokal; sonst nur Basis setzen |
| neu | gleich | hochladen mit erwarteter Revision |
| neu | neu | gleicher Hash → nur Basis setzen; sonst Konflikt |
| keine Basis | – | nur lokal → hochladen; nur Cloud → herunterladen; gleich → Basis setzen; sonst Konflikt |
| fehlt lokal | vorhanden | herunterladen |

- „Lokal neu“ = Hash ≠ Basis-Hash. „Cloud neu“ = Revision ≠ Basis-Revision.
- Lehnt der Anbieter einen Upload wegen veralteter Revision ab, wird neu entschieden.
- Zeiten im Konfliktdialog: Wii-Seite aus der letzten Spielsitzung, Cloud-Seite aus dem Paket (`saved`, `device`).

### 6. Anbieter (M27–M29)

- **Google Drive (M27):** Gerätecode-Flow, Scope `drive.file`, Ordner „newlauncher“ im Drive des Nutzers.
- **OneDrive (M28):** Gerätecode-Flow über `consumers`, Scope `Files.ReadWrite.AppFolder offline_access`,
  App-Ordner. Upload mit `If-Match`.
- **Dropbox (M29):** PKCE ohne Umleitung; der Nutzer gibt den angezeigten Code auf der Kopplungsseite oder per
  Tastatur ein. Zugriffsart „App folder“.
- Genau ein Anbieter ist aktiv (`config.ini [cloud] provider = none|drive|onedrive|dropbox`).
- **Anbieterwechsel (M28):** Hat der neue Anbieter schon Pakete, einmal fragen: „Wii überschreibt alles / Cloud
  behalten / einzeln fragen“. Danach alle Einheiten abgleichen. Der Abgleich-Zustand gilt je `<anbieter>:<konto>`.
- **Alle abgleichen** (Cloud-Menü, M28): alle Einheiten der Bibliothek, abbrechbar, mit Zähler.
- **Abmelden:** Token widerrufen (wo der Anbieter es kann), lokalen Zustand löschen.

### 7. Kopplung und Eingabe (M29)

- **Kopplungsseite:** Die Wii startet für die Dauer des Dialogs einen kleinen HTTP-Server im LAN (Port 4301) und
  zeigt einen QR-Code mit `http://<wii-ip>:4301/<token>`. Token: 128 Bit CSPRNG, eine Übermittlung, 5 Minuten.
  Seite auf Deutsch und Englisch.
- Dropbox-Code geht im Klartext (ohne den PKCE-Verifier, der die Wii nie verlässt, wertlos).
- **Tastatur:** Kleinbuchstaben, Ziffern, Symbole, bis 128 Zeichen, Maskierung für Passwörter.

### 8. Sicherheit und Privatsphäre

- Tokens liegen verschlüsselt in `<data>/cloud/tokens.bin` (AES-CBC + HMAC-SHA256, Schlüssel aus Geräte-ID und
  Salt). **Das ist nur Verschleierung:** Wer SD-Karte und Wii hat, kommt an die Tokens. Echten Schutz geben die
  engen Scopes (Drive nur eigene Dateien, OneDrive und Dropbox nur der App-Ordner) und Abmelden mit Widerruf.
- Tokens, Codes, E-Mail-Adressen und Passwörter erscheinen nie im Log (`redactSecret`, Host-Test, Smoke-Prüfung).
- Client-IDs und das Google-Client-Secret stehen nur in `config.ini [cloud]` (`drive_client_id`,
  `drive_client_secret`, `onedrive_client_id`, `dropbox_app_key`), nie im Repo oder im Build. Ohne Eintrag ist der
  Anbieter ausgegraut mit Verweis auf `docs/cloud-setup.md`.
- Nur HTTPS mit Zertifikatsprüfung; ohne TLS-Build ist die Cloud aus (`NL_CLOUD off notls`).
- Zufall für PKCE, Tokens der Kopplungsseite und Schlüssel nur aus `psa_generate_random` (Netz-Thread).

## Nicht-Ziele

- Kein Teilen von Spielständen, keine Links, kein Verlauf in der Cloud über die letzte Version hinaus.
- Keine Savestates, keine echte GC-Speicherkarte, keine Wii-`data.bin`-Erzeugung.
- Kein MEGA (vom Nutzer gestrichen).
- Kein Abgleich während ein Spiel läuft. Kein Löschen in der Cloud aufgrund lokaler Löschung.
- Kein Schreiben außerhalb von `title/<hi>/<lo>/data`.

## Randbedingungen

- `src/core/` bleibt ohne `<ogc/...>`, alles Entscheidende ist host-testbar (Anbieter gegen ein eingespeistes
  `HttpTransport`).
- Kein `throw`, kein RTTI, 0 Warnungen mit und ohne TLS.
- `wtls_*` nur im Netz-Thread.
- Eigenbau statt fremder Bibliotheken für Deflate, JSON und QR (wie `sha256`/`aes128`).
- Uhr für Token-Abläufe: monotone Zeit (`gettime`), nicht die RTC.

## Meilensteine

| | Inhalt |
|---|---|
| M26 | Ziele 1–4: Einheiten, Paket, Sicherung, Wiederherstellung mit Journal, NAND-Patch, Save-Manager-Szene |
| M27 | Ziel 5 + Google Drive: HTTP-Methoden, JSON, Tokens, Gerätecode, Abgleich vor dem Start, Upload danach, Fake-Server |
| M28 | OneDrive, Anbieterwechsel, Alle abgleichen, Abmelden |
| M29 | Dropbox, Kopplungsseite, QR-Code, erweiterte Tastatur |

## Für den Umsetzer offen

- Feinheiten der Szenen (Layout, Fokus), solange die Ziele erfüllt sind.
- Kompressionsstufe von Deflate (feste Huffman-Codes reichen).
- Wie der ISFS-Patch das Rechtemuster findet (Spike S1 auf Hardware steht aus; bis dahin `Emulated` in Dolphin,
  `NotFound` auf Hardware ohne Muster).
