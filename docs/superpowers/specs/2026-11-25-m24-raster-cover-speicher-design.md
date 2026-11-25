# M24: Kompakteres Raster, Kacheln im Cover-Format überall, Cover-Speicher auf SD, Löschen ohne Hänger

Stand: 2026-11-25. Baut auf M23 (`2026-11-25-m23-cover-cache-zeiger-design.md`) auf.

## Absicht (Wünsche des Nutzers nach dem M23-Test auf der Wii)

1. Cover sollen nach Konsolengeneration **neu → alt** geladen werden.
2. Spiele **standardmäßig nach Namen** sortiert, aber auch **nach Jahr** sortierbar.
3. In **Zuletzt/Favoriten** sollen die Kacheln ebenfalls zu den Cover-Maßen passen. Entschieden: Variante **B**
   (feste Spalten, Reihe so hoch wie ihr höchstes Cover), die Cover **vertikal zentriert** in der Reihe.
4. Die **Konsolen-Plakette** auf der Kachel verschwindet, sobald ein Cover geladen ist.
5. „Cover-Cache löschen“ **hing** auf der Wii.
6. Bestätigen durch **A gedrückt halten** statt zweimal A.
7. Die Cover sollen **mehr vom Bildschirm** einnehmen: kein Spalt zwischen den Kacheln, weniger Luft oben und unten,
   alles etwas kompakter.
8. Der Cover-Cache soll **bevorzugt auf der SD-Karte** liegen, in den Einstellungen umstellbar.

## Ziele

### 1. Download-Reihenfolge
Aufträge nach Generationsrang, innerhalb einer Konsole wie bisher (Titelreihenfolge der Bibliothek):
**Wii → GC → GBA → N64 → GBC → SNES → GB → NES** (Generation absteigend, bei gleicher Generation Heimkonsole vor
Handheld). Gilt für „Fehlende Cover laden“ und den automatischen Abruf. Über mehrere Geräte hinweg wird gemeinsam
sortiert (nicht Gerät für Gerät).

### 2. Sortierung
Die Einstellung „Sortierung“ (Titel, Jahr, Zuletzt gespielt, Meistgespielt; Standard Titel) gibt es seit M17. Neu:
eine Schnellumschaltung **Name ↔ Jahr** in der A–Z-Leiste (Taste 2 bzw. X). Dort schaltet **−** (Wiimote; L bei
GC/Classic) um, ebenso A auf der Plakette „Sortierung: Name“ links über der Buchstabenreihe (Zeiger). Im Hauptbild-
schirm ist − schon „vorige Konsole“ und bleibt das (Umsetzer-Entscheidung, weil die Wiimote keine freie Taste hat).
Die Wahl wird wie die Einstellung gespeichert (`[library] sort`). Steht die Einstellung auf Zuletzt/Meistgespielt,
wechselt die Umschaltung auf Titel. Hinweis (Toast) „Sortiert nach Name“ / „Sortiert nach Jahr“. Favoriten, Zuletzt
und Suche behalten ihre eigene Reihenfolge (wie bisher).

### 3. Gemischte Listen im Cover-Format
In Favoriten, Zuletzt und Suche hat jede Kachel die Höhe ihres Konsolenformats (`coverAspect`); die Spalten bleiben.
Jede Reihe ist so hoch wie ihre höchste Kachel, kürzere Kacheln stehen **vertikal zentriert** in der Reihe.
Navigation (links/rechts/hoch/runter, Seite) bleibt spaltenweise wie bisher. Scrollen, Treffertest, Randscrollen und
„Reihe ganz sichtbar“ rechnen mit den unterschiedlichen Reihenhöhen. Konsolenlisten (alle Kacheln gleich) verhalten
sich wie in M23.

### 4. Plakette
Die Konsolen-Plakette (oben links) blendet mit dem Cover aus (Deckkraft 1 − Cover-Alpha). Die Plakette „Disc“
(Spiel im Laufwerk) bleibt, ebenso Favoritenpunkt und Schloss.

### 5. Löschen ohne Hänger
Das Löschen läuft in einem Hintergrund-Thread; der Hauptbildschirm zeichnet weiter und zeigt einen Fortschritt
(„Cover werden gelöscht … n“, Zahl der schon gelöschten Dateien). Ablauf: laufender Cover-Download wird abgebrochen, ohne den Hauptthread zu blockieren
(Abbruch anfordern, warten im Frame-Takt); der Cover-Cache im Speicher wird geleert; dann zählt und löscht der Thread
die Dateien. Während des Löschens startet kein Cover-Download und kein Cover wird geladen. Ende: Log
`NL_COVERS_CLEARED files= failed= ms=`, Toast mit Anzahl.

### 6. Halten zum Bestätigen
„Cover-Cache löschen“: **A gedrückt halten (1 s)** löst aus; der Wert der Zeile zeigt dabei einen Fortschrittsbalken
bzw. „A halten …“. Loslassen vor Ablauf bricht ab; Fokuswechsel und B ebenso. Gleiches gilt für A über den Zeiger
auf der Zeile. Das Zweimal-A aus M23 entfällt.

### 7. Kompakteres Raster
- Spalt zwischen den Kacheln **16 → 4 px** (854) bzw. **12 → 4 px** (640).
- Kachelbreite 854: **104 → 128 px** (Rand 33), 640: **88 → 99 px** (Rand 13); weiterhin 6 Spalten, Breite exakt
  gefüllt. 5:7-Höhe 179 bzw. 139. Bei 854 passen so zwei volle 5:7-Reihen in die Höhe (Umsetzer-Entscheidung:
  breiter ginge, dann wäre die zweite Reihe immer angeschnitten).
- Luft über und unter dem Raster (`kGridTopPad`) **12 → 4 px**.
- Konsolenleiste oben **56 → 48 px** (Pillen 8 px vom Rand), Infoleiste unten **72 → 60 px** (kleinster Wert, bei dem
  Titel (22 px) und Zeile (16 px) und die runden Knöpfe (44 px) passen).
- Cover-Einzug in der Kachel (`kCoverInset`) **4 → 2 px**.
- Der Fokus (Rahmen, Vergrößerung) darf über den kleinen Spalt in Nachbarkacheln ragen; die fokussierte Kachel wird
  zuletzt gezeichnet.

### 8. Cover-Speicher
Neue Einstellung **„Cover-Speicher“: SD-Karte (Standard) / Spielgerät**, `[covers] storage = sd|device`.
- **SD:** Alle Cover (`covers_src`, `cache/covers`, `missing.txt`) liegen unter `sd:/newlauncher/…`, für alle Geräte
  gemeinsam. Ist die SD nicht beschreibbar oder fehlt sie, gilt „Spielgerät“ (Log `NL_WARN`).
- **Spielgerät:** wie bisher (`coverRoot` je Gerät).
- Umstellen wirkt nach dem Verlassen der Einstellungen ohne Neustart: Cover-Dienst und Wurzeln werden neu gesetzt.
- Keine Übernahme vorhandener Cover vom anderen Ort; der automatische Abruf bzw. „Fehlende Cover laden“ holt sie neu.
  „Cover-Cache löschen“ löscht am **eingestellten** Ort.

## Nicht-Ziele
- Variable Kachelbreiten („Regal“, Variante A).
- Eigener Sortierdialog oder weitere Sortierarten.
- Übernahme/Verschieben vorhandener Cover zwischen SD und Spielgerät.
- Parallele Downloads.

## Randbedingungen
UI-Texte DE/EN; `src/core` ohne `<ogc/…>`; kein throw/RTTI; 0 Warnungen mit TLS und `--no-tls`; `wtls_*` nur im
NetService-Thread; FAT-Zugriffe aus dem Hintergrund-Thread wie beim Scan.

## Ansätze
- Gemischte Listen: (A) Regal mit variabler Breite, (B) feste Spalten mit Reihenhöhe = höchste Kachel.
  **Vom Nutzer gewählt: B**, Kacheln vertikal zentriert.
- Löschen: (a) im Hauptthread mit Zwischen-Frames, (b) eigener Thread mit Fortschritt. Gewählt (b): FAT-Löschen ist
  je Datei blockierend und nicht zerlegbar.
- Cover-Speicher: (a) SD lesen mit Rückfall auf das Gerät, (b) nur der eingestellte Ort. Gewählt (b): ein Ort,
  einfaches Löschen, keine doppelten Bestände.

## Für den Umsetzer offen
Genaue Darstellung von Fortschrittsbalken und Toast; Name der Thread-Hilfsklasse.
