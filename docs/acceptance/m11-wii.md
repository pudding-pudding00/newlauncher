# Hardware-Abnahme M11 (HTTPS über wii_tls)

Abnahme auf einer echten Wii, gegen die **Release-DOL mit TLS** (`python tools/build_wii.py`, ohne
`--no-tls`). Dolphin hat HTTPS gegen den lokalen Testserver, Zertifikatsfehler und den Build ohne TLS
bereits gezeigt. Echte Zeitmessung, SNTP über das Internet, die echten Server (GameTDB, OSCWii) und das
Verhalten bei Netzausfall zeigt nur die Hardware.

Die Abnahme läuft zusammen mit den Hardware-Suiten von `wii_tls` (unten). Jeder Punkt bekommt PASS
oder FAIL; bei FAIL eine kurze Notiz und eine Zeile unter [Befunde](#befunde).

## Vorbereitung

1. Bauen: `python tools/build_wii.py`. Die Ausgabe muss 0 Warnungen zeigen.
2. Karte bespielen und Messung einschalten:
   ```bash
   python tools/wii_deploy.py E: --perf on
   ```
   Damit steht in `sd:/newlauncher/config.ini` unter `[debug]` die Zeile `perf = 1`. Jeder erfolgreiche
   TLS-Handshake schreibt dann `NL_PERF tls heap=<Bytes> ms=<Dauer>` ins Log.
3. Keine `tls_ca`-Zeile in `[debug]`. Die Test-CA gehört nur zu Dolphin-Läufen; auf der Wii zählt allein das
   eingebaute Mozilla-Bundle.
4. In `[covers]` und `[emulators]` keine eigenen Quellen. Der TLS-Build nimmt dann die Standardquellen über
   HTTPS.
5. Nach der Abnahme: `python tools/wii_deploy.py E: --perf off`

## Hardware-Suiten von wii_tls

Die vendorte Kopie unter `third_party/wii_tls` enthält keine Tests (`VENDORED.txt`: `removed=tests,…`).
Die Suiten laufen deshalb aus einem Checkout des Upstream-Repos `pudding-pudding00/wii_tls`, und zwar auf dem
Commit aus `VENDORED.txt`. PC und Wii im selben Netz, Homebrew Channel offen, IP aus der HBC-Ecke. Bauen und
starten (Details im README dort):

```bash
cmake -S . -B build-wii -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Wii.cmake && cmake --build build-wii -j
python tools/hwtest.py --wii <IP> --dol build-wii/tests/hw/wtls_hwtest.dol --suite selftest
```

Den errno-Fix aus M11 (siehe unten) vorher auch im Upstream-Checkout einspielen. Sonst prüfen die Suiten
eine andere `plat.h` als die, die im Launcher läuft.

| Suite | Ergebnis | Notiz |
|---|---|---|
| `selftest` | | |
| `plat` | | |
| `time` | | |
| `entropy` | | |
| `tls` (gegen den PC-Testserver) | | |
| `realworld` | | |

M11 hat in `third_party/wii_tls/src/plat/plat.h` die errno-Werte auf die von newlib umgestellt (EAGAIN 11,
EINPROGRESS 119, EALREADY 120, EISCONN 127). libogc übersetzt die IOS-Codes vorher in diese Werte. Schlägt
`plat` oder `tls` beim Verbindungsaufbau fehl, zuerst diesen Punkt ansehen.

## Checkliste

| # | Schritt | Erwartet | Ergebnis |
|---|---|---|---|
| W1 | Ein Wii-Spiel starten | Booter mit eigenem `progress.c` startet, Fortschrittsbalken sichtbar, Spiel läuft | |
| W2 | Einen VC-Kanal starten | Startet unverändert wie in M4 | |
| W3 | Wii-Uhr in den Systemeinstellungen auf 2000 stellen, dann Cover laden | Log enthält `NL_TLS_TIMESYNC ok`; der Download gelingt, kein `NL_NET_FAIL clock` | |
| W4 | Cover-Cache leeren, 20 Cover über HTTPS laden lassen, dabei **nicht** scrollen | UI flüssig; `check_perf.py` zeigt TLS mit PASS (Handshake ≤ 3000 ms, Heap ≤ 160 KiB) | |
| W5 | Emulatorliste öffnen und einen Emulator installieren | Liste kommt über HTTPS (`NL_EMU_LIST ok`), Installation endet mit `ok` | |
| W6 | Während eines Downloads HOME → Zurück zum HBC | Sauberer Ausstieg innerhalb weniger Sekunden, kein Hänger | |
| W7 | Router-Stecker ziehen, dann Cover oder Emulatorliste anfordern | Verständlicher Fehlertext (Verbindung/Zeitüberschreitung), Launcher bleibt bedienbar | |
| W8 | Einstellungen → „Über“ | Zeigt Version, „HTTPS: An“ (EN „On“) und die Komponentenliste mit Lizenzen | |

### Hinweise zu den Punkten

- **W3:** Die Wii hat keinen Uhrabgleich übers Netz. Steht die Uhr vor dem Gültigkeitsbeginn des
  Zertifikats, gleicht der Launcher sie einmal per SNTP ab (höchstens zwei Versuche je Sitzung) und
  versucht die Verbindung erneut. Dolphin beantwortet SNTP nicht, deshalb gibt es diesen Nachweis nur
  auf der Hardware. Danach die Uhr zurückstellen.
- **W4:** `heap` ist die Differenz von `mallinfo().uordblks` vor und nach dem Handshake. Der Wert
  stimmt nur, wenn der Haupt-Thread in der Zeit nichts anfordert oder freigibt; daher nicht scrollen.
  Negative Werte wertet `check_perf.py` als „nicht messbar“. Auswertung:
  ```bash
  python tools/check_perf.py E:/newlauncher/log.txt
  ```
  Die Zeile „TLS“ kommt in die Tabelle unten. Spec v2 4.6 nennt Zielwerte, kein hartes Budget: eine
  Überschreitung ergibt WARN und wird unter Befunde notiert.
- **W6:** Ein laufender TLS-Handshake lässt sich nicht unterbrechen. Im schlimmsten Fall dauert der
  Ausstieg bis zu 18 s (8 s Verbindung + 10 s Handshake). Länger ist FAIL.
- **W7:** Erwartete Log-Zeilen: `NL_NET_FAIL dns`, `connect` oder `timeout`. Kein Rückfall auf HTTP.

## Messwerte (W4)

| Kriterium | Messwert | Budget | Ergebnis |
|---|---|---|---|
| TLS | | Heap ≤ 163840 B, Handshake ≤ 3000 ms (Ziel) | |

## Befunde

| # | Punkt | Beschreibung | Folge |
|---|---|---|---|
