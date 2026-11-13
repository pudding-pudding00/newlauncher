# Spike M17: Wii-Jugendschutz aus `SYSCONF` (`IPL.PC`)

**Frage (Spec v2 10.5):** Lässt sich die Jugendschutz-Stufe und die PIN der Wii aus `/shared2/sys/SYSCONF` lesen, damit die Option „Wii-Jugendschutz übernehmen“ (`[parental] use_wii = 1`) möglich ist?

**Ergebnis: positiv, mit einer offenen Hardware-Prüfung.** Layout und PIN sind öffentlich dokumentiert; die Zuordnung des Organisations-Bytes ist nur aus der DSi-Spezifikation abgeleitet und wird in der Abnahme W7 auf echter Hardware bestätigt.

## Quellen (öffentlich)

- WiiBrew, „/shared2/sys/SYSCONF“: Dateiformat und Eintrag `IPL.PC` – <https://wiibrew.org/wiki//shared2/sys/SYSCONF>
- TWL ParentalControl specification (2008-05-16): Codes der Bewertungsstellen, die das DSi-Menü nutzt (gleiche Generation, gleiche Nummerierung wie die Bewertungs-Tabelle im Wii-Disc-Header).

## Dateiformat `SYSCONF`

- Immer 0x4000 Byte, Big Endian. `0x0000` Magic `SCv0`, `0x0004` u16 Anzahl der Einträge, ab `0x0006` je Eintrag ein u16-Offset. `0x3FFC` Magic `SCed`.
- Eintrag: Byte 0 = Typ in den oberen 3 Bit, Namenslänge − 1 in den unteren 5 Bit; dann der Name; dann die Daten.
- Typen: 1 BIGARRAY (u16 Länge − 1, dann Daten), 2 SMALLARRAY (u8 Länge − 1, dann Daten), 3 BYTE, 4 SHORT, 5 LONG, 6 LONGLONG, 7 BOOL.

## `IPL.PC` (SMALLARRAY, 0x4A Byte Nutzdaten)

| Offset (Nutzdaten) | Größe | Bedeutung |
|---|---|---|
| 0x00 | 1 | Jugendschutz aktiv: `0x80` an, `0x00` aus |
| 0x01 | 1 | Bewertungsstelle (siehe unten) |
| 0x02 | 1 | höchste erlaubte Altersstufe als Alter; `0x14` (20) = keine Einschränkung |
| 0x03 | 4 | PIN, 4 ASCII-Ziffern, nicht nullterminiert |
| 0x07 | 1 | Geheimfrage (0–5) |
| 0x08 | 0x40 | Geheimantwort, UTF-16BE |
| 0x48 | 2 | Länge der Antwort in Zeichen |

(WiiBrew zählt das Längen-Byte des SMALLARRAY als Offset 0x00 mit; die Tabelle oben beginnt bei den Nutzdaten.)

## Bewertungsstelle (vorläufig, aus der TWL-Spezifikation)

| Wert | Stelle | newlauncher |
|---|---|---|
| 0 | CERO | `RatingSystem::Cero` |
| 1 | ESRB | `RatingSystem::Esrb` |
| 3 | USK | `RatingSystem::Usk` |
| 4 | PEGI | `RatingSystem::Pegi` |
| 6 | PEGI Portugal | `RatingSystem::Pegi` |
| 7 | PEGI/BBFC (UK) | `RatingSystem::Pegi` |
| sonst | (BBFC alt, PEGI Finnland alt, OFLC, …) | unbekannt → das System aus `[parental] system` bleibt |

## Umsetzung (Task 6)

- `core/parental`: `sysconfItem(file, "IPL.PC")` findet die Nutzdaten in der Datei; `parseIplPc(payload)` liefert aktiv, Stelle, Alter und PIN. Alles ohne `<ogc/...>`, Host-getestet mit einer erzeugten `SYSCONF`-Fixture.
- `platform`: liest die Datei per ISFS (`/shared2/sys/SYSCONF`, nur lesend). Der Launcher schreibt nie in `SYSCONF`.
- Mit `use_wii = 1` gelten Stufe und PIN der Wii; die Wii-PIN wird nur im Speicher gehasht, mit einem Zufallssalz, das bei jedem Start neu entsteht (nicht das Salz aus `config.ini`), und nie geloggt. Ist der Jugendschutz der Wii aus, ist auch die Kindersicherung des Launchers aus.
- Altersstufe 20 = keine Einschränkung → `maxAge = 255`.

## Offen für die Hardware-Abnahme (W7)

- Organisations-Byte auf einer PAL-Wii mit PEGI und, falls verfügbar, einer US-Wii mit ESRB prüfen.
- Bedeutung des Alters-Bytes bestätigen (höchste erlaubte Stufe, nicht „ab dieser Stufe gesperrt“).
