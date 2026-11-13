# Spike M18: Banner – Umfang und Wii-Disc-Banner

Stand 2026-11-13. Quellen nur als Formatbeschreibung gelesen, nichts übernommen: WiiBrew („Wii Animations“, „U8 archive“, „IMD5“, „TPL“, „BNS“, „Wii Disc“, „Ticket“), Dolphin-Quelltext (`DiscIO/VolumeWii`, `IOS/ES`, `IOS/IOSC`) und die libogc-Header (`ogc/es.h`).

## 1. Umfang BRLYT/BRLAN

### Abschnitte im Layout (`RLYT`)

| Abschnitt | M18 |
|---|---|
| `lyt1` (Layout-Größe) | gelesen: Breite, Höhe |
| `txl1` (Texturnamen) | gelesen |
| `mat1` (Materialien) | gelesen: Name, erste Textur-Map, erste Textur-SRT, Materialfarbe; TEV-Stufen werden zu „Textur × Vertexfarbe × Materialfarbe“ angenähert |
| `pan1` (Gruppierung) | gelesen: Transformation, Alpha, Sichtbarkeit, Ursprung |
| `pic1` (Bild) | gelesen und gezeichnet: wie `pan1` plus Vertexfarben, Material, UV |
| `pas1`/`pae1` (Kinder auf/zu) | gelesen: Baum |
| `grp1`, `grs1`/`gre1` (Gruppen) | übersprungen, gezählt |
| `txt1`, `wnd1`, `bnd1`, `usd1`, `fnl1` | übersprungen, gezählt |

### Animations-Tags (`RLAN`, Abschnitt `pai1`)

| Tag | Ziel | M18 |
|---|---|---|
| `RLPA` | Position, Rotation, Skalierung, Größe (Index 0–9) | ja |
| `RLVC` | Vertexfarben (0x00–0x0F), Pane-Alpha (0x10) | ja |
| `RLTS` | Textur-SRT | ja |
| `RLTP` | Texturwechsel (Stufen, keine Zwischenwerte) | ja |
| `RLMC` | Materialfarbe | ja |
| `RLIM`, `RLVI` | indirekte Textur, Sichtbarkeit | nein, übersprungen |

Die Keyframes sind Hermite-Kurven (Bild, Wert, Steigung); `RLTP` und `RLVI` nutzen Stufen.

### Abdeckung an echten Bannern

Im Dolphin-Dev-NAND liegt nur der eigene Testkanal (`00010001/4e4c5443`), also kein fremdes Kanal-Banner. Die Zählung, welcher Anteil der Panes mit dem Umfang oben gezeichnet wird, holt die Hardware-Abnahme W3 an drei echten Kanälen nach. Der Launcher loggt dazu je Banner `NL_BANNER panes=<gezeichnet>/<gesamt> skipped=<abschnitte>`.

## 2. Wii-Disc-Banner

`opening.bnr` liegt im FST der Spielpartition. Die Partition ist in Clustern zu 0x8000 Byte verschlüsselt: 0x400 Byte Hash-Bereich, 0x7C00 Byte Daten, AES-128-CBC mit dem Titelschlüssel. Der Titelschlüssel steht im Ticket am Anfang der Partition, selbst mit dem Common Key verschlüsselt.

### Kandidat A: d2x-cIOS mit Fragmentliste (`DI_OpenPartition` + `DI_Read`)

- **Ablauf:** IOS auf das cIOS wechseln, Fragmentliste setzen (M9), Partition öffnen, entschlüsselt lesen, danach zurück auf das HBC-IOS.
- **Zeitbedarf:** IOS-Wechsel je ein bis zwei Sekunden, zweimal.
- **Risiko:** Ein IOS-Wechsel schließt USB, ISFS, Netz und den Cover-Thread. Für eine Detailansicht ist das nicht tragbar.
- **vWii:** nur mit cIOS.
- **Urteil:** verworfen.

### Kandidat B: `ES_Decrypt` mit dem Schlüssel-Handle des Common Key

- **Was die API bietet:** libogc deklariert `ES_Decrypt(u32 keynum, u32* iv, const void* src, u32 size, void* dst)` und die Handles `ES_KEY_COMMON = 4` und `ES_KEY_KOREAN = 11`. IOS hält den Schlüssel selbst. Der Launcher übergibt nur die 16 verschlüsselten Bytes des Titelschlüssels und den IV und bekommt den Titelschlüssel zurück. Der Common Key verlässt IOS nie und steht nirgends im Quelltext.
- **Ablauf:**
  1. Partitionstabelle bei 0x40000 lesen.
  2. Ticket am Partitionsanfang lesen:
     - verschlüsselter Titelschlüssel bei 0x1BF;
     - Titel-ID bei 0x1DC, als IV mit 8 Nullbytes aufgefüllt;
     - Schlüsselindex bei 0x1F1 (0 = Common, 1 = Korea).
  3. `ES_Decrypt` liefert den Titelschlüssel.
  4. Datenbereich der Partition liegt bei Offset 0x2B8 ×4. Je Cluster gilt der IV aus den Bytes 0x3D0–0x3DF des verschlüsselten Clusters.
  5. Software-AES-128-CBC über die 0x7C00 Datenbytes.
  6. Darin liegen Disc-Kopf und FST. FST-Offset und -Größe stehen bei 0x424/0x428, Dateioffsets ×4.
  7. `opening.bnr` ist ein IMET-Kopf plus U8, also derselbe Weg wie bei Kanälen ab Task 3.
- **Zeitbedarf:** FST und `opening.bnr` sind zusammen meist unter 1 MiB, also unter 40 Cluster. Software-AES auf dem Broadway schafft das in deutlich unter einer Sekunde. Es läuft im Cover-Thread.
- **Risiko:**
  - Ob das HBC-IOS (IOS58) `ES_Decrypt` mit Handle 4 für ein Nicht-System-Programm zulässt, ist nicht belegt. Dolphin lässt es zu, echte Hardware ist offen.
  - Schlägt der Aufruf fehl, fällt der Launcher auf das Cover zurück (`NL_BANNER_FALLBACK cover es`). Kein IOS-Wechsel, kein Zustand geht verloren.
- **vWii:** gleiche API. Wii-U-Konsolen nutzen im vWii den Wii-Common-Key-Slot, offen bis zur Hardware-Abnahme.

### Kandidat C: Hardware-AES unter AHBPROT

Verworfen: Registerzugriff auf die AES-Engine konkurriert mit IOS, und das ist ohne Nutzen gegenüber B.

## 3. Entscheidung

- **Umfang:** wie in Abschnitt 1. Die Pane-Abdeckung wird auf der Wii in W3 gemessen.
- **Wii-Disc-Banner: positiv mit Vorbehalt, Weg B.** Task 9 wird umgesetzt:
  - Plattformfrei und host-getestet sind Ticket lesen, Cluster entschlüsseln und das FST mit Offsets ×4.
    - AES-128 ist eigener Code und wird gegen die FIPS-197-Testvektoren geprüft.
    - Das Entschlüsseln des Titelschlüssels ist eine eingespeiste Funktion.
    - Host-Tests bauen eine Partition mit einem frei gewählten Test-Schlüssel, ganz ohne Common Key.
  - Auf der Wii liefert `ES_Decrypt` den Titelschlüssel.
  - Kein Dolphin-Smoke: Ein eigenes, „echt“ verschlüsseltes Test-Image bräuchte den Common Key beim Erzeugen. Die Hardware-Abnahme W6 prüft den Weg. Lehnt IOS den Aufruf ab, bleibt es beim Cover, und W6 hält das fest.
- **Nicht im Quelltext:** kein Common Key, kein Hash davon, kein Fremd-Tool.
