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

## Fehlersuche

| Anzeige / Log | Ursache |
|---|---|
| Anbieter ausgegraut, „Keine Client-ID in config.ini“ | `drive_client_id` fehlt oder ist leer |
| „Dieser Build hat kein HTTPS“, Log `NL_CLOUD off notls` | Build ohne TLS |
| „Anmeldung abgelaufen – bitte neu anmelden“, Log `NL_CLOUD <einheit> skip auth` | Refresh-Token ungültig (App noch im Status „Testen“, Zugriff entzogen) |
| Log `NL_CLOUD <einheit> skip offline` | kein Netz; das Spiel startet ohne Abgleich, der Upload folgt später |
| Log `NL_CLOUD_LINK failed` | Client-ID/Secret falsch oder Typ nicht „Fernseher und Geräte mit begrenzter Eingabe“ |

## Tests ohne echtes Konto

`python tools/dolphin_smoke.py --cloud-fake drive --cloud-step link|push|pull|conflict|refresh|offline` startet
einen Drive-Fake (`tools/cloud_fake.py`) über HTTPS mit der Test-CA und prüft den Ablauf in Dolphin. Die Fake-ID
und das Fake-Secret stehen nur im Werkzeug.
