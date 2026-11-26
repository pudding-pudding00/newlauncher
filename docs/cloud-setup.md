# Cloud-Spielstände einrichten

newlauncher gleicht Spielstände mit **einem** Cloud-Anbieter ab: Vor dem Spielstart holt er einen neueren Stand,
nach dem Spielen lädt er Änderungen hoch, bei einem Konflikt fragt er. Dafür braucht jeder Nutzer eine eigene
App-Registrierung beim Anbieter. Client-ID und Secret stehen **nur** in `sd:/newlauncher/config.ini`, nie im Repo
und nie in einem CI-Build. Ohne Client-ID ist der Anbieter in den Einstellungen ausgegraut.

Voraussetzung: ein Build **mit TLS** (`python tools/build_wii.py`). Der Build ohne TLS blendet die Cloud aus.

## Google Drive

### 1. Projekt und Zustimmungsbildschirm

1. <https://console.cloud.google.com/> öffnen und ein neues Projekt anlegen, z. B. „newlauncher“.
2. **APIs & Dienste → Bibliothek:** „Google Drive API“ suchen und **Aktivieren**.
3. **APIs & Dienste → OAuth-Zustimmungsbildschirm** (bzw. „Google Auth Platform“):
   - Nutzertyp **Extern**, App-Name frei wählbar, eigene E-Mail als Support- und Entwicklerkontakt.
   - **Datenzugriff / Bereiche:** nur `https://www.googleapis.com/auth/drive.file` hinzufügen. Dieser Bereich gibt
     newlauncher nur Zugriff auf Dateien, die newlauncher selbst angelegt hat (Ordner „newlauncher“), nicht auf den
     Rest des Drive.
4. **Veröffentlichungsstatus auf „In Produktion“ stellen** („Publish app“). Im Status „Testen“ verfallen
   Refresh-Tokens nach **7 Tagen**, dann muss man sich auf der Wii neu anmelden. Für `drive.file` verlangt Google
   keine Prüfung; der Anmeldebildschirm zeigt dann höchstens den Hinweis „Google hat diese App nicht überprüft“,
   den man als Besitzer bestätigen kann.

### 2. Client-ID anlegen

1. **APIs & Dienste → Anmeldedaten → Anmeldedaten erstellen → OAuth-Client-ID.**
2. Anwendungstyp **„Fernseher und Geräte mit begrenzter Eingabe“** („TVs and Limited Input devices“).
3. Client-ID (`…apps.googleusercontent.com`) und Client-Secret (`GOCSPX-…`) kopieren. Beim Gerätecode-Ablauf gilt
   das Secret laut Google nicht als geheim, es gehört trotzdem nicht ins Repo und nicht in Screenshots.

### 3. config.ini

In `sd:/newlauncher/config.ini` eintragen:

```ini
[cloud]
provider = drive
drive_client_id = 1234567890-abc.apps.googleusercontent.com
drive_client_secret = GOCSPX-...

[general]
; optional: erscheint im Konfliktdialog anderer Geräte als Herkunft des Cloud-Stands (höchstens 32 Zeichen)
device_name = Wii Wohnzimmer
```

`provider = drive` lässt sich auch in den Einstellungen setzen; die Client-ID muss in der Datei stehen.

### 4. Auf der Wii anmelden

1. **Einstellungen → Cloud-Spielstände → Anbieter: Google Drive → Anmelden.**
2. Am Handy oder PC die angezeigte Adresse (`google.com/device`) öffnen und den Code eingeben.
3. Konto wählen und Zugriff erlauben. Die Wii zeigt „Angemeldet“.

Die Tokens liegen verschlüsselt in `sd:/newlauncher/cloud/tokens.bin`. Das ist nur Verschleierung: Der Schlüssel
entsteht aus der Geräte-ID der Wii. Wer SD-Karte **und** Wii hat, kommt an den Zugriff auf den Ordner
„newlauncher“, nicht an den Rest des Drive. **Abmelden** löscht die Tokens auf der SD. Den Zugriff ganz entziehen kann
man unter <https://myaccount.google.com/permissions>.

### Was in der Cloud liegt

Im Drive-Ordner `newlauncher` liegt je Spielstand eine Datei `<einheit>.zip`, z. B. `wii-RMCP.zip` oder
`gb-tetris-world-rev-1-d2ff925e.zip`. Das ZIP enthält die rohen Spielstand-Dateien und `newlauncher-save.ini`
(Gerät, Zeit, Inhalts-Hash) und lässt sich am PC öffnen. Lokal merkt sich newlauncher den letzten gemeinsamen
Stand in `sd:/newlauncher/cloud/state.ini`.

## OneDrive

### 1. App in Microsoft Entra registrieren

1. <https://entra.microsoft.com/> (oder <https://portal.azure.com/>) mit einem Microsoft-Konto öffnen.
   **Identität → Anwendungen → App-Registrierungen → Neue Registrierung.**
2. Name frei wählbar, z. B. „newlauncher“. Unterstützte Kontotypen: **„Nur persönliche Microsoft-Konten“**
   (newlauncher meldet sich am Endpunkt `consumers` an, Geschäfts- und Schulkonten gehen damit nicht).
   Umleitungs-URI leer lassen.
3. **Authentifizierung → Erweiterte Einstellungen → „Öffentliche Clientflows zulassen“** („Allow public client
   flows“) auf **Ja** stellen und speichern. Ohne diesen Schalter lehnt Microsoft den Gerätecode ab.
4. **API-Berechtigungen → Berechtigung hinzufügen → Microsoft Graph → Delegierte Berechtigungen:**
   `Files.ReadWrite.AppFolder` und `offline_access`. Mit `Files.ReadWrite.AppFolder` sieht newlauncher nur seinen
   eigenen App-Ordner, nicht den Rest des OneDrive. Ein Client-Secret braucht man **nicht**.
5. **Übersicht:** die **Anwendungs-ID (Client-ID)** kopieren (Form `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`).

### 2. config.ini

```ini
[cloud]
provider = onedrive
onedrive_client_id = xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

Beide Anbieter dürfen eingetragen sein; aktiv ist immer nur der unter `provider`.

### 3. Auf der Wii anmelden

1. **Einstellungen → Cloud-Spielstände → Anbieter: OneDrive → Anmelden.**
2. Am Handy oder PC `microsoft.com/link` öffnen, den Code eingeben, mit dem Microsoft-Konto anmelden und zustimmen.

### Was in der Cloud liegt

Im OneDrive unter **Apps → <App-Name>** liegt je Spielstand eine Datei `<einheit>.zip` wie bei Google Drive. Gerät,
Zeit und Inhalts-Hash stehen zusätzlich in der Beschreibung der Datei (`nl1 hash=… saved=… device=…`); diese Zeile
bitte nicht von Hand ändern.

**Abmelden** löscht die Tokens auf der SD. Microsoft bietet keinen Widerruf einzelner Refresh-Tokens an; den Zugriff
ganz entziehen kann man unter <https://account.live.com/consent/Manage>.

## Dropbox

### 1. App in der Dropbox App Console anlegen

1. <https://www.dropbox.com/developers/apps> mit dem Dropbox-Konto öffnen → **Create app**.
2. **Scoped access**, Zugriff **App folder** (newlauncher sieht dann nur seinen eigenen Ordner, nicht den Rest der
   Dropbox). Name frei wählbar, z. B. „newlauncher-<deinname>“ (der Name muss bei Dropbox eindeutig sein).
3. Reiter **Permissions:** `files.content.read`, `files.content.write` und `account_info.read` anhaken und unten
   **Submit** drücken. Rechte, die erst nach einer Anmeldung dazukommen, gelten erst nach einer neuen Anmeldung.
4. Reiter **Settings → OAuth 2 → Allow public clients (Implicit Grant & PKCE)** auf **Allow** lassen bzw. stellen.
   newlauncher meldet sich mit PKCE an und braucht **kein** App-Secret. Redirect-URIs bleiben leer.
5. Den **App key** kopieren (nicht das App secret).

Eine neue App steht bei Dropbox im Status „Development“ und darf so bis zu 500 Konten verbinden; das reicht für den
eigenen Gebrauch, eine Freigabe („Apply for production“) ist nicht nötig.

### 2. config.ini

```ini
[cloud]
provider = dropbox
dropbox_app_key = abcdefghijklmno
```

### 3. Auf der Wii koppeln

1. **Einstellungen → Cloud-Spielstände → Anbieter: Dropbox → Anmelden.** Die Kopplungsszene zeigt einen QR-Code
   und darunter die Adresse `http://<wii-ip>:4301/<token>`.
2. Den QR-Code mit dem Handy scannen (Handy im **selben WLAN** wie die Wii). Die Kopplungsseite öffnet sich; dort
   auf **Bei Dropbox zulassen** tippen, anmelden und zustimmen. Dropbox zeigt danach einen Code.
3. Den Code kopieren, auf der Kopplungsseite einfügen und **Code senden**. Die Wii meldet „Angemeldet“.

Die Kopplungsseite lebt nur, solange die Szene offen ist (höchstens 5 Minuten), und nimmt genau **eine** Einsendung
an; danach meldet sie „Diese Seite ist nicht mehr aktiv“. Lehnt Dropbox einen Code ab, zeigt die Szene „Code abgelehnt“ und die
Seite nimmt einen neuen an.

**Ohne Handy im WLAN:** **Y** (bzw. **1**) schaltet auf den direkten QR-Code der Dropbox-Freigabe (geht mit jedem Gerät mit
Internet, auch über Mobilfunk). Den Code, den Dropbox danach zeigt, gibt man mit **A** über die Bildschirmtastatur
ein (Ebenen abc / ABC / 123 / # über die Ebenen-Taste, bis 128 Zeichen).

Der Code allein ist wertlos: Er passt nur zu dem geheimen PKCE-Schlüssel, der die Wii nie verlässt. Die Seite läuft
deshalb über einfaches `http://` im Heimnetz; Tokens gehen nie über die Kopplungsseite.

### Was in der Cloud liegt

In der Dropbox unter **Apps → <App-Name>** liegt je Spielstand eine Datei `<einheit>.zip`. Dropbox kennt kein
Beschreibungsfeld; Gerät, Zeit und Inhalts-Hash stehen nur in `newlauncher-save.ini` im ZIP. Als Revision dient die
Dropbox-`rev`; ein Upload überschreibt nur, wenn die Datei seit dem letzten Abgleich unverändert ist (sonst
entscheidet newlauncher neu bzw. fragt).

**Abmelden** widerruft das Token bei Dropbox (Log `NL_CLOUD_UNLINKED revoke=ok`) und löscht es auf der SD.

## Anbieter wechseln und „Alle abgleichen“

- **Anbieter wechseln:** Anbieter umstellen (◀▶ in der Zeile „Anbieter“), dann **Anmelden**. Der alte Anbieter wird
  dabei abgemeldet (Google und Dropbox: Token widerrufen). Direkt nach der Anmeldung gleicht newlauncher **alle** Spielstände der
  Bibliothek mit dem neuen Anbieter ab: Was nur auf der Wii liegt, wird hochgeladen; was nur in der Cloud liegt, wird
  geholt (mit Sicherung vorher). Gibt es Spielstände auf beiden Seiten mit verschiedenem Inhalt, fragt ein Dialog
  einmal für alle: **Wii-Stände nehmen**, **Cloud-Stände nehmen** oder **Beim Spielstart fragen** (Standard; dann
  fragt das Start-Tor je Spiel).
- **Alle abgleichen** (Zeile im Cloud-Menü) macht denselben Abgleich jederzeit von Hand. Während er läuft, heißt die
  Zeile „Abgleich abbrechen“; der Status zeigt „Abgleich 3/12 …“ und am Ende, wie viele Stände geholt und
  hochgeladen wurden.

## Fehlersuche

| Anzeige / Log | Ursache |
|---|---|
| Anbieter ausgegraut bzw. beim Umschalten übersprungen, „Keine Client-ID in config.ini“ | `drive_client_id`, `onedrive_client_id` bzw. `dropbox_app_key` fehlt oder ist leer |
| „Dieser Build hat kein HTTPS“, Log `NL_CLOUD off notls` | Build ohne TLS |
| „Anmeldung abgelaufen – bitte neu anmelden“, Log `NL_CLOUD <einheit> skip auth` | Refresh-Token ungültig (App noch im Status „Testen“, Zugriff entzogen) |
| Log `NL_CLOUD <einheit> skip offline` | kein Netz; das Spiel startet ohne Abgleich, der Upload folgt später |
| Log `NL_CLOUD_LINK failed` | Google: Client-ID/Secret falsch oder Typ nicht „Fernseher und Geräte mit begrenzter Eingabe“; Microsoft: „Öffentliche Clientflows zulassen“ aus oder Kontotyp nicht „persönliche Microsoft-Konten“; Dropbox: App key falsch, „Allow public clients“ aus oder Kopplung abgelaufen (5 Minuten) |
| „Abgemeldet, Widerruf nicht bestätigt“, Log `NL_CLOUD_UNLINKED revoke=fail` | Tokens auf der SD sind gelöscht, der Anbieter hat den Widerruf aber nicht bestätigt (offline); Zugriff bei Bedarf in den Kontoeinstellungen entziehen |
| Szene „Code abgelehnt“, Log `NL_CLOUD_LINK badcode` | Dropbox-Code falsch abgetippt, schon benutzt oder abgelaufen; neuen Code über den Link holen |
| Handy erreicht die Kopplungsseite nicht, Log nur `NL_PAIR listen 4301` | Handy nicht im selben WLAN, Gast-WLAN mit Client-Isolation oder Mobilfunk; **Y** (direkter QR) und **A** (Tastatur) nehmen |
| Kopplungsseite „nicht mehr aktiv“, Log `NL_PAIR reject` | zweite Einsendung oder Szene schon geschlossen; Anmelden neu starten |
| Log `NL_CLOUD_SYNCALL offline` | „Alle abgleichen“ ohne Netz; nichts wurde verändert |

## Tests ohne echtes Konto

`python tools/dolphin_smoke.py --cloud-fake drive|onedrive|dropbox --cloud-step
link|push|pull|conflict|refresh|offline|syncall|switch|unlink` startet einen Drive-, Graph- bzw. Dropbox-Fake
(`tools/cloud_fake.py`) über HTTPS mit der Test-CA und prüft den Ablauf in Dolphin. Die Fake-IDs und das Fake-Secret
stehen nur im Werkzeug. Bei Dropbox spielt `tools/cloud_smoke.py` das Handy: Es öffnet die Kopplungsseite über
`127.0.0.1:4301` (Dolphin reicht die Wii-Sockets an den PC durch), folgt dem Link zur Fake-Freigabe und sendet den
Code. Nur mit Dropbox: `--cloud-step badcode` (erst ein falscher Code, dann der richtige) und `pairtwice` (zweite
Einsendung → 410).
