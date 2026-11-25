# M23: Cover-Cache löschen, Scrollen mit dem Zeiger, Kacheln im Cover-Format, Messung der Cover-Downloads

Stand 2026-11-25. Kleiner Meilenstein nach M22 (Retro-Cover), aus dem Hardware-Test mit 808 Spielen.

## Absicht

1. **Cover-Cache löschen:** Der Nutzer will alle zwischengespeicherten Cover verwerfen können, etwa um falsche
   Cover neu zu laden.
2. **Scrollen mit dem Zeiger:** Mit dem Wiimote-Zeiger sieht man je Konsole nur die ersten drei Reihen; das
   Raster scrollt bisher nur mit dem Steuerkreuz.
3. **Kacheln im Cover-Format (Nachtrag des Nutzers):** „Die Höhe der Titelobjekte soll zum Seitenverhältnis der
   Cover passen.“ Bisher ist jede Kachel 5:7 hoch; ein SNES-Cover (quer) liegt darin als schmales Band.
4. **Downloads beschleunigen:** Die Messung auf der Wii (2026-11-25) zeigt: Die Keep-Alive-Verbindung wird schon
   wiederverwendet (`reuse=1`, ein Handshake von 122 ms). Die Zeit geht in die Übertragung (libretro-PNGs
   300–550 KB bei etwa 400 KB/s) und rund 170 ms Speichern je Cover. Ein Umbau auf parallele Verbindungen bräuchte
   wii_tls-Änderungen und wird separat entschieden. M23 liefert nur die Messung (`[debug] perf`).

## Ziele

- Einstellungen: neue Aktionszeile „Cover-Cache löschen“ / „Clear cover cache“ direkt nach „Fehlende Cover laden“.
  Erstes A schärft die Zeile (Wert „A: wirklich löschen?“ / „A: really delete?“), zweites A löscht; ein
  Fokuswechsel oder B nimmt die Schärfung zurück.
- Gelöscht werden je Cover-Wurzel (alle Geräte): `newlauncher/covers_src/<konsole>/*` und
  `newlauncher/cache/covers/<konsole>/*` für alle Konsolen außer Kanälen, dazu `cache/covers/missing.txt`.
  Auch heruntergeladene PNGs liegen in `covers_src`; nur so lädt „Fehlende Cover laden“ danach wirklich neu.
- Vorher laufende Cover-Downloads abbrechen, den Cover-Thread anhalten und den Textur-Cache leeren; die Kacheln
  zeigen danach Platzhalter. Log `NL_COVERS_CLEARED files=<n> failed=<m>`.
- Kein automatisches Neuladen danach (das macht der Nutzer per „Fehlende Cover laden“ oder beim nächsten Start).
- Zeiger: steht er im Rasterbereich im obersten bzw. untersten Streifen von 40 px, scrollt das Raster stetig
  nach oben bzw. unten (Geschwindigkeit wächst zum Rand hin, höchstens etwa eine Reihe je 0,25 s). Der Fokus folgt
  weiter der Kachel unter dem Zeiger.
- Kachelhöhe je Konsolenliste nach dem typischen Cover-Format (gemessen an libretro-Boxarts 2026-11-25:
  SNES/N64 512×357, GB/GBC/GBA 512×512, NES ~359×512; Wii/GC 5:7): SNES/N64 10:7 quer, GB/GBC/GBA 1:1, sonst 5:7.
  Die Breite bleibt (6 Spalten), die 5:7-Höhe wird skaliert (854: 146 → 104 bzw. 73 px; 640: 123 → 88 bzw. 62 px).
  Favoriten, Zuletzt und Suche (gemischt) bleiben 5:7.
- Jedes Cover wird mit seinem echten Seitenverhältnis eingepasst (nie verzerrt, nie beschnitten). Den Inhalt findet
  der Launcher an den ganz transparenten 4×4-CMPR-Blöcken der 200×280-Textur; so gilt das auch für vorhandene
  `.tex`-Dateien ohne Neuumwandlung. Detailansicht genauso.
- `[debug] perf`: je Cover `NL_PERF cover get status= ms= hs= bytes= reuse=` und `NL_PERF cover save ms=`.

## Nicht-Ziele

- Parallele Downloads, kleinere Bilder, eigene Thumbnail-Quelle.
- Getrenntes Löschen nur der Downloads (PNG-Herkunft wird nicht gespeichert).
- Kacheln je Spiel unterschiedlich hoch (Reihen wären nicht bündig); breitere Kacheln für Querformate.

## Ansätze

- Rückfrage: (a) eigener Dialog, (b) zweimal A in der Zeile. Gewählt (b): kein neues Overlay, testbar in der
  Settings-Szene, gleiches Muster wie das Überschreiben in der Cheat-Ansicht.
- Cover-Format: (a) Seitenverhältnis als Feld in der .tex-Datei, (b) aus den transparenten CMPR-Blöcken ablesen.
  Gewählt (b): keine Formatänderung, alte Caches funktionieren sofort.
- Zeiger-Scrollen: (a) Randstreifen, (b) B halten und ziehen. Gewählt (a): bekannt aus dem Wii-Menü, kein
  Konflikt mit B = Zurück.

## Randbedingungen

AGENTS.md: UI-Texte DE und EN, `src/core` ohne `<ogc/…>`, 0 Warnungen mit und ohne TLS, Host-Tests für core/ui,
Hardware-Liste `docs/acceptance/m23-wii.md`.
