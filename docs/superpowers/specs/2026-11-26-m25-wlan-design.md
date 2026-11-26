# M25: WLAN robuster, genauer angezeigt, mit Info-Fenster

Stand: 2026-11-26. Anlass: Das WLAN kommt manchmal nicht hoch, im HBC genauso wie in newlauncher. Die Ursache
liegt unter libogc (IOS, WLAN-Modul, Router). Der Launcher soll den Fehler deshalb abfedern und erklären.

## 1. Verbindungsaufbau (platform/net_service)

- Nach jedem fehlgeschlagenen `net_init()` folgt `net_deinit()`, damit der nächste Versuch frisch beginnt.
- Pausen zwischen Versuchen: 0,5 / 1 / 2 / 4 s, danach 4 s (`wifiRetryDelayMs`).
- Versuche je Durchgang (`wifiInitAttempts`): erster Check nach dem Start 4, vor einem Netzauftrag 3,
  Hintergrund-Check 2.
- Nach erfolgreichem `net_init()` liefert `if_configex(…, true, 1)` IP, Netzmaske und Gateway für das
  Info-Fenster (dessen `net_init` ist dann ein No-op). Ein Fehlercode < 0 wird wie bisher in `lastNetCode`
  gemerkt. Die MAC kommt aus `net_get_mac_address`.
- Der Hintergrund-Check (main, nur wenn kein Auftrag läuft) hat folgenden Abstand (`wifiProbeIntervalMs`):
  verbunden 5 s; getrennt je nach Zahl der Fehlschläge in Folge 5 / 5 / 10 / 20 s, dann 30 s.
- Mit NL_TLS folgt auf jeden Neuaufbau `wtls_net_reset()`.

## 2. Genauere Anzeige

- Neuer Zustand `WifiStatus::Limited`: Die Wii hat eine IP, aber der Router (Gateway) antwortet nicht.
- Bei bestehender Verbindung prüft jeder Hintergrund-Check den Router: ein nicht blockierender TCP-Connect auf
  Gateway:80 mit 1,5 s Zeitlimit. Verbunden **oder abgewiesen** (RST, `-ECONNREFUSED`) heißt, der Router ist
  erreichbar. Gemessen wird die Antwortzeit in ms. Zeitüberschreitung oder ein anderer Fehler heißt
  „antwortet nicht“ (`classifyGatewayConnect` in core).
- Erst **zwei** Router-Fehlschläge in Folge ergeben `Limited`, ein einzelner verlorener SYN reicht nicht.
- `Limited` löst höchstens einmal je 60 s einen Neuaufbau aus (`net_deinit`, dann `if_configex`). Antwortet der
  Router danach wieder, ist der Zustand `Connected`. Filtert er Port 80 nur, bleibt er `Limited`, aber ohne
  Dauer-Neuaufbau.
- Startwert ist `Connecting`, nicht `Disconnected`: Vor dem ersten Check gibt es keinen roten Punkt.
- Symbol: verbunden 4 Balken (Akzent); eingeschränkt 2 Balken (Akzent) und ein gelber Punkt (fest 230/160/30,
  das Theme hat keine Warnfarbe); verbindet: ein Lauflicht über die Balken (≈ 250 ms je Balken); getrennt: graue
  Balken und roter Punkt. Die Beschriftung darunter bleibt „WLAN“ / „Wi-Fi“.
- Hinweise als Toast in der Infoleiste (`WifiWatch`), nur beim Zustandswechsel: erster Fehlschlag nach dem
  Start, Verbindung verloren, wieder verbunden, eingeschränkt.

## 3. Info-Fenster (ui/wifi_scene)

- Zeiger auf dem WLAN-Symbol hebt es hervor wie Home und Zahnrad. A öffnet das Overlay „WLAN“ / „Wi-Fi“.
- Zeilen (DE/EN): Zustand, IP, Netzmaske, Router (Gateway), Router-Antwort (ms oder „keine“), MAC,
  letzter Fehlercode von `net_init` (nur wenn < 0), Fehlschläge in Folge, verbunden seit (mm:ss),
  letzte Prüfung vor n s, HTTPS bereit (nur TLS-Build).
- Ist der Zustand nicht `Connected`, folgen Tipps: WPA2-AES statt WPA3/Mischbetrieb; 2,4 GHz mit eigenem
  Namen; PMF/802.11w aus oder optional; Kanal 1, 6 oder 11; feste IP in den Wii-Einstellungen;
  Wii ganz vom Strom trennen (Standby/WiiConnect24 aus).
- A im Fenster: „Neu verbinden“ (`AppCommand::WifiReconnect`): `net_deinit` und ein Durchgang wie beim Start.
  B/Home schließt. ▲▼ blättert. Die Werte aktualisieren sich, solange das Fenster offen ist.
- Ein Neuaufbau beendet eine laufende Fernsteuerungs-Verbindung (TCP 4300). Der Server bindet danach neu
  (vorhandenes Verhalten).

- Zeiger auf dem Symbol: Hintergrund `accentSoft` wie bei Home und Zahnrad.

## 4. Daten zwischen den Threads

`NetService::snapshot()` gibt ein `WifiSnapshot` (core/wifi_state.hpp) unter dem Mutex zurück. Der Netz-Thread
schreibt es nur unter dem Mutex. `wtls_*` bleibt im Netz-Thread.

Log-Marker (ohne IP und MAC, wie die Systeminfo): `NL_WIFI ok gw=yes|no gw_ms=<n> reconnects=<n>` beim ersten
Verbinden, `NL_WIFI fail code=<n>`, `NL_WIFI lost code=<n>`, `NL_WIFI limited gw_fails=<n>`,
`NL_WIFI restored gw_ms=<n>`, `NL_CMD wifi_reconnect`.

## 5. Tests und Abnahme

- Host: `wifi_retry`/`wifi_state` (Pausen, Abstände, `WifiWatch`, `classifyGatewayConnect`, Formatierung),
  `wifi_scene` (Zeilen DE/EN, Tipps nur ohne Verbindung, A → `WifiReconnect`, B → pop),
  `main_scene` (Klick aufs Symbol öffnet das Fenster).
- Dolphin: Smoke mit dem Marker `NL_WIFI` (Dolphin simuliert ein verbundenes WLAN).
- Wii: `docs/acceptance/m25-wii.md`.
