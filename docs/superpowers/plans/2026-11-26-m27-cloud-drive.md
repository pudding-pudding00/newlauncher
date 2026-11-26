# M27 Cloud-Kern + Google Drive – Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Ziel 5 der Spec mit Google Drive als erstem Anbieter: anmelden per Gerätecode, vor dem Spielstart den
Cloud-Stand abgleichen (holen, hochladen oder fragen), nach dem Spielen geänderte Einheiten im Hintergrund hochladen.

**Architecture:** Alles Entscheidende liegt host-testbar in `src/core`: Kodierungen, JSON, HTTP-Typen mit einem
eingespeisten `HttpTransport`, Gerätecode-Flow, verschlüsselter Token-Speicher, Drive-Protokoll, Entscheidungstabelle,
Abgleich-Zustand, Outbox und der Ablauf vor dem Start/nach dem Spielen. `src/platform` liefert `httpSend` über
wii_tls, Zufall und Geräte-ID und einen `CloudWorker` im Netz-Thread. Die UI bekommt Cloud-Menü, Gerätecode-Szene,
Abgleich-Szene und den Konfliktdialog (vorhandener `ChoiceDialog`). Ein Python-Fake-Server spielt Google in den Smokes.

**Tech Stack:** C++20, devkitPPC/libogc, wii_tls v1.0.0 (`<wiitls/http.h>`), doctest, Python 3 (`http.server` + `ssl`),
Dolphin-Smokes mit Test-CA.

**Spec:** `docs/superpowers/specs/2026-11-26-cloud-spielstaende-design.md` (Ziel 5, Ziel 6 Drive, Ziel 8)

## Global Constraints
- UI-Texte DE und EN; `src/core` ohne `<ogc/…>`; kein throw/RTTI; 0 Warnungen mit und ohne TLS.
- `wtls_*` und `psa_generate_random` nur im Netz-Thread. Nur HTTPS mit Zertifikatsprüfung; kein Schalter dagegen.
- Ohne TLS-Build ist die Cloud aus: Einstellungszeile ausgegraut, Log `NL_CLOUD off notls`.
- `config.ini [cloud]`: `provider = none|drive` (M27), `drive_client_id`, `drive_client_secret`. Nie im Repo, nie im
  Build. Ohne `drive_client_id` ist Drive ausgegraut, Hinweis auf `docs/cloud-setup.md`.
- Scope genau `https://www.googleapis.com/auth/drive.file`; Ordner „newlauncher“ im Drive-Stamm; Paket `<einheit>.zip`.
- Dateien unter `<data>/cloud/`: `tokens.bin`, `state.ini`, `session.ini`, `pending.ini`.
- Tokens, Gerätecodes, Client-Secret und Konto-E-Mail erscheinen nie im Log und nie im Klartext auf der SD.
- Zeitlimit Cloud-Abfrage vor dem Start **20 s**; B = „Ohne Abgleich spielen“ jederzeit.
- Token-Abläufe über die monotone Uhr (`NetService::clockMs()`), nie über die RTC.
- Paketgröße in der Cloud höchstens `kMaxSaveUnitBytes * 2` (gepackt).
- Marker: `NL_CLOUD off notls|noprovider|noclient`, `NL_CLOUD_LINKED drive`, `NL_CLOUD_PULL ok|fail <einheit>`,
  `NL_CLOUD_PUSH ok|fail|keep <einheit>`, `NL_CLOUD_SKIP <grund> <einheit>` (Gründe `offline timeout auth user nand
  error`), `NL_CLOUD_CONFLICT <einheit>`, `NL_CLOUD_REFRESH ok|fail`, `NL_CLOUD_NOOP <einheit>`.

## Review Focus
1. Access-Token läuft mitten im Abgleich ab (401): genau ein Refresh und eine Wiederholung; scheitert der Refresh
   (`invalid_grant`), startet das Spiel ohne Abgleich (`NL_CLOUD_SKIP auth`), das Cloud-Menü zeigt „Neu anmelden“.
2. Netz bricht während des Downloads ab oder liefert ein fremdes/kaputtes Paket (andere Einheit, Hash ≠
   `appProperties.hash`): nichts wird geschrieben, Spiel startet ohne Abgleich.
3. Cloud-Revision ändert sich zwischen Abfrage und Upload (zweite Wii): Upload meldet `RevMismatch`, es wird neu
   entschieden, im Zweifel Konflikt statt Überschreiben.
4. Kein Token, Code oder Secret im Log oder in einer Datei unter `<data>/cloud/` außer verschlüsselt in `tokens.bin`.
5. Ein `pending.ini`-Eintrag überlebt jeden Fehler (offline, NAND gesperrt, Absturz mitten im Upload) und fällt erst
   nach bestätigtem Upload heraus; derselbe Eintrag zweimal wird nur einmal hochgeladen.

---

### Task 1: Base64, URL-Kodierung, HMAC-SHA256
**Files:** `src/core/base64.{hpp,cpp}`, `src/core/url_encode.{hpp,cpp}`, `src/core/hmac.{hpp,cpp}`,
`src/core/cover_download.cpp` (anonymes Percent-Encoding ersetzen), Tests `tests/host/core/{base64,url_encode,hmac}_test.cpp`
- Produces: `std::string base64Encode(std::span<const u8>, bool url = false)` (url = `-_` ohne `=`),
  `std::optional<std::vector<u8>> base64Decode(std::string_view, bool url = false)`;
  `std::string percentEncode(std::string_view)` (RFC 3986, unreserviert `A-Za-z0-9-._~`),
  `std::string formEncode(std::span<const std::pair<std::string_view, std::string_view>>)` (`k=v&…`, Leerzeichen `%20`);
  `std::array<u8, 32> hmacSha256(std::span<const u8> key, std::span<const u8> msg)`;
  `bool constantTimeEqual(std::span<const u8>, std::span<const u8>)`.
- [ ] Tests (FAIL): RFC 4648 §10 alle sieben Vektoren (`""`, `"f"` … `"foobar"`) hin und zurück, URL-Variante
      `{0xfb,0xff}` → `"-_8"`, ungültige Zeichen → nullopt; `percentEncode("a b/ä")` = `"a%20b%2F%C3%A4"`;
      `formEncode({{"grant_type","urn:ietf:params:oauth:grant-type:device_code"}})` =
      `"grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"`; RFC 4231 Testfälle 1, 2 und 6
      (Schlüssel länger als 64 Bytes); `constantTimeEqual` mit ungleicher Länge → false.
- [ ] Umsetzen; Cover-Tests bleiben grün. Commit `feat: Base64, URL-Kodierung und HMAC-SHA256`.

### Task 2: JSON lesen und schreiben
**Files:** `src/core/json.{hpp,cpp}`, `tests/host/core/json_test.cpp`
- Produces: `struct JsonValue` (Typ `Null|Bool|Number|String|Array|Object`, `double num`, `std::string str`,
  `std::vector<JsonValue> items`, `std::vector<std::pair<std::string, JsonValue>> members`;
  `const JsonValue* get(std::string_view key) const`, `std::string_view string(std::string_view key) const` (leer, wenn
  fehlt/kein String), `std::optional<i64> integer(std::string_view key) const` (auch aus String wie `"123"`, Drive liefert
  `version`/`size` als String));
  `Result<JsonValue> parseJson(std::string_view, JsonLimits = {})` mit `JsonLimits{depth = 32, nodes = 4096, bytes = 1 MiB}`;
  `class JsonWriter { obj(); end(); key(sv); str(sv); num(i64); boolean(bool); std::string take(); }`.
- [ ] Tests (FAIL): Objekte/Arrays verschachtelt; Escapes `\" \\ \/ \b \f \n \r \t`, `ä` → UTF-8, Surrogatpaar
      `😀` → 4 Bytes, einzelnes Surrogat → Fehler; Zahlen `-0`, `1e3`, `12.5`; Müll nach dem Wert, fehlendes
      Komma, Steuerzeichen im String, Tiefe 33, 4097 Knoten → Fehler; `integer("size")` aus `"size":"123"`;
      Writer-Rundlauf mit Sonderzeichen über `parseJson`.
- [ ] Umsetzen, Commit `feat: JSON-Parser mit Grenzen und Writer`.

### Task 3: HTTP-Typen, Transport, `httpSend` über wii_tls
**Files:** `src/core/http_types.hpp`, `src/core/multipart.{hpp,cpp}`, `tests/host/support/fake_transport.hpp`,
`tests/host/core/http_types_test.cpp`, `src/platform/http_client.{hpp,cpp}`
- Produces (core): `enum class HttpMethod : u8 { Get, Post, Put, Patch, Delete }`;
  `struct HttpRequest { HttpMethod method; std::string url; std::vector<std::pair<std::string, std::string>> headers; std::vector<u8> body; u32 maxBody = 1 MiB; }`;
  `struct HttpResponse { u16 status = 0; NetError error = NetError::Other; std::vector<std::pair<std::string, std::string>> headers; std::vector<u8> body; std::string_view header(std::string_view name) const; }`
  (`status == 0` = keine Antwort; `header` ohne Groß/klein); `class HttpTransport { public: virtual ~HttpTransport() = default; virtual HttpResponse send(const HttpRequest&, const volatile bool& cancel) = 0; };`
  `std::vector<u8> multipartRelated(std::string_view boundary, std::string_view json, std::span<const u8> data)` und
  `std::string multipartContentType(std::string_view boundary)`.
- Produces (Test-Hilfe): `class FakeTransport : public HttpTransport` mit `expect(HttpMethod, std::string urlPrefix, HttpResponse)`
  (Skript in Reihenfolge), `std::vector<HttpRequest> seen`, `bool done() const`; unerwartete Anfrage → `status = 599`.
- Produces (platform): `HttpResponse httpSend(const HttpRequest&, const volatile bool* cancel, const HttpLog& log)`
  über `wtls_http_request` mit `method`, eigenen Headern, `body`/`body_len`, `max_redirects = -1` (keine Umleitung,
  Spike S5), Antwort-Header über `wtls_http_header_at`, Body bis `maxBody`; ohne `NL_TLS` → `{0, TlsMissing}`;
  `class WtlsTransport final : public HttpTransport` ruft `httpSend`. Bestehende `httpGet`-Logik (Test-CA, SNTP-Versuch,
  Timeouts) teilt sich den Öffnungsteil mit `httpSend`, nicht kopieren.
- [ ] Tests (FAIL): `header("content-type")` findet `Content-Type`; `multipartRelated` = genau
      `--b\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n{…}\r\n--b\r\nContent-Type: application/zip\r\n\r\n<bytes>\r\n--b--\r\n`;
      FakeTransport: Skript in Reihenfolge, unerwartete Anfrage → 599, `done()` erst nach allen.
- [ ] Umsetzen; Wii-Build mit und ohne TLS (0 Warnungen). Commit `feat: HTTP-Methoden mit Headern und Body`.

### Task 4: Gerätecode-Flow (OAuth 2.0 RFC 8628) und Tokens
**Files:** `src/core/oauth_device.{hpp,cpp}`, `tests/host/core/oauth_device_test.cpp`
- Consumes: `HttpTransport`, `formEncode`, `parseJson`.
- Produces: `struct OAuthEndpoints { std::string deviceUrl, tokenUrl; std::string clientId, clientSecret, scope; }`
  und `OAuthEndpoints googleEndpoints(std::string_view base, std::string_view id, std::string_view secret)`
  (`base` leer → `https://oauth2.googleapis.com`; `deviceUrl = base + "/device/code"`, `tokenUrl = base + "/token"`);
  `struct DeviceCode { std::string deviceCode, userCode, verifyUrl; u32 expiresInS, intervalS; }` (Google `verification_url`,
  Microsoft `verification_uri`, beide lesen; `interval` fehlt → 5);
  `struct TokenSet { std::string access, refresh; u64 expiresAtMs = 0; }`;
  `enum class PollState : u8 { Pending, SlowDown, Denied, Expired, Ok, Failed }`;
  `Result<DeviceCode> requestDeviceCode(HttpTransport&, const OAuthEndpoints&, const volatile bool& cancel)`;
  `PollState pollToken(HttpTransport&, const OAuthEndpoints&, std::string_view deviceCode, u64 nowMs, TokenSet& out, const volatile bool& cancel)`;
  `PollState refreshToken(HttpTransport&, const OAuthEndpoints&, TokenSet& io, u64 nowMs, const volatile bool& cancel)`
  (`invalid_grant` → `Denied`; Antwort ohne neues `refresh_token` behält das alte);
  `bool tokenFresh(const TokenSet&, u64 nowMs)` (60 s Puffer).
- [ ] Tests (FAIL) mit echten Antwortformen: Google-Gerätecode `{"device_code":"…","user_code":"ABCD-EFGH","verification_url":"https://www.google.com/device","expires_in":1800,"interval":5}`;
      Formular enthält `client_id`, `scope`; Poll mit `{"error":"authorization_pending"}` → Pending,
      `slow_down` → SlowDown, `access_denied` → Denied, `expired_token` → Expired, 200 mit `access_token`,
      `expires_in:3599`, `refresh_token` → Ok und `expiresAtMs = now + 3599000`; Refresh ohne neues refresh_token
      behält das alte; Status 0 (offline) → Failed; `tokenFresh` 59 s vor Ablauf → false.
- [ ] Umsetzen, Commit `feat: Gerätecode-Anmeldung und Token-Refresh`.

### Task 5: Verschlüsselter Token-Speicher und `redactSecret`
**Files:** `src/core/token_store.{hpp,cpp}`, `tests/host/core/token_store_test.cpp`
- Consumes: `Aes128` (CBC), `sha256`, `hmacSha256`, `constantTimeEqual`, `TokenSet`.
- Produces: `struct StoredTokens { std::string provider, account; TokenSet tokens; }` (expiresAtMs wird nicht gespeichert:
  monotone Uhr gilt nur je Sitzung → nach dem Laden `expiresAtMs = 0`, erster Zugriff refresht);
  `std::vector<u8> sealTokens(const StoredTokens&, std::span<const u8> deviceId, std::span<const u8, 16> salt, std::span<const u8, 16> iv)`;
  `std::optional<StoredTokens> unsealTokens(std::span<const u8> blob, std::span<const u8> deviceId)`;
  `std::string redactSecret(std::string_view s)` → `"<geheim:N>"` (N = Länge, nie Zeichen daraus).
- Format: `"NLTK"`, Version `1`, salt[16], iv[16], Länge u32 BE, AES-128-CBC(PKCS#7) über eine INI
  (`provider`, `account`, `access`, `refresh`), HMAC-SHA256[32] über alles davor (encrypt-then-MAC).
  Schlüssel: `k = hmacSha256(salt, deviceId)`, `encKey = hmacSha256(k, "enc")[0..16)`, `macKey = hmacSha256(k, "mac")`.
- [ ] Tests (FAIL): Rundlauf; ein gekipptes Bit in salt, iv, Chiffrat oder MAC → nullopt; falsche Geräte-ID → nullopt;
      der Blob enthält weder access noch refresh im Klartext; `redactSecret("ya29.abc")` = `"<geheim:8>"`.
- [ ] Umsetzen, Commit `feat: Token-Speicher verschlüsselt mit Geräteschlüssel`.

### Task 6: Anbieter-Schnittstelle und Google Drive
**Files:** `src/core/cloud_provider.hpp`, `src/core/drive_provider.{hpp,cpp}`, `tests/host/core/drive_provider_test.cpp`
- Consumes: Tasks 2–4.
- Produces: `enum class CloudStatus : u8 { Ok, NotFound, RevMismatch, Unauthorized, Offline, Failed }`;
  `struct CloudEntry { std::string id, rev, hash, device; u64 saved = 0, size = 0; }` (hash/device/saved aus den
  Datei-Eigenschaften, nicht aus dem ZIP);
  `class CloudProvider { public: virtual ~CloudProvider() = default; virtual CloudStatus account(std::string& id, const volatile bool&) = 0; virtual CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool&) = 0; virtual CloudStatus download(const CloudEntry&, std::vector<u8>& zip, const volatile bool&) = 0; virtual CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta&, std::string_view expectRev, CloudEntry& out, const volatile bool&) = 0; };`
  (`expectRev` leer = Datei darf noch nicht existieren);
  `class DriveProvider final : public CloudProvider` mit Konstruktor `(HttpTransport&, OAuthEndpoints, TokenSet&, std::function<u64()> nowMs, std::string apiBase = "https://www.googleapis.com")`
  und `bool tokensChanged() const` (nach Refresh speichern).
- Protokoll (Pfade relativ zu apiBase, `Authorization: Bearer <access>`):
  - Konto: `GET /drive/v3/about?fields=user(permissionId)` → `permissionId` (keine E-Mail, Ziel 8).
  - Ordner: `GET /drive/v3/files?q=<name='newlauncher' and mimeType='application/vnd.google-apps.folder' and 'root' in parents and trashed=false>&fields=files(id)`;
    fehlt er → `POST /drive/v3/files` mit `{"name":"newlauncher","mimeType":"application/vnd.google-apps.folder"}`.
    Ordner-ID je Sitzung zwischenspeichern.
  - stat: `GET /drive/v3/files?q=<'<ordner>' in parents and name='<unit>.zip' and trashed=false>&fields=files(id,version,size,appProperties)`;
    0 Treffer → NotFound; mehrere → der mit höchster `version`.
  - download: `GET /drive/v3/files/<id>?alt=media`, `maxBody = kMaxSaveUnitBytes * 2`.
  - upload: zuerst `stat`; `rev` ≠ `expectRev` (oder vorhanden bei leerem expectRev) → RevMismatch, ohne zu senden.
    Neu: `POST /upload/drive/v3/files?uploadType=multipart&fields=id,version,size,appProperties`, Metadaten
    `{"name":"<unit>.zip","parents":["<ordner>"],"appProperties":{"hash":…,"saved":"<unix>","device":…,"unit":…}}`.
    Vorhanden: `PATCH /upload/drive/v3/files/<id>?uploadType=multipart&fields=…` mit Metadaten ohne `parents`.
    Paket > 5 MiB: `uploadType=resumable` (POST bzw. PATCH mit Metadaten-JSON, `X-Upload-Content-Length`), `Location`
    aus der Antwort, ein `PUT` mit dem ganzen Paket.
  - Jede Anfrage: vorher `tokenFresh` sonst `refreshToken`; Antwort 401 → einmal refresh und dieselbe Anfrage
    wiederholen; Refresh `Denied` → Unauthorized. Status 0 → Offline; 5xx/sonst → Failed.
  - `q` mit `percentEncode`; Hochkommas in Namen gibt es nicht (Einheiten-IDs sind `[a-z0-9-]`, sonst Failed).
- [ ] Tests (FAIL, FakeTransport): Ordner fehlt → POST Ordner, danach stat NotFound; stat liest `version` als rev,
      `appProperties.hash`; Upload neu = multipart-POST mit `parents` und Bearer-Header; Upload vorhanden mit passendem
      rev = PATCH; rev weicht ab → RevMismatch und keine Upload-Anfrage; 6-MiB-Paket → resumable POST + PUT an die
      `Location`; 401 → Refresh-POST an `tokenUrl`, Wiederholung mit neuem Token, `tokensChanged()`; Refresh
      `invalid_grant` → Unauthorized; Status 0 → Offline; Download-Antwort größer als die Grenze → Failed.
- [ ] Umsetzen, Commit `feat: Google-Drive-Anbieter`.

### Task 7: Entscheidungstabelle, Abgleich-Zustand, Outbox
**Files:** `src/core/cloud_sync.{hpp,cpp}`, `src/core/cloud_state.{hpp,cpp}`, `src/core/cloud_outbox.{hpp,cpp}`,
Tests `tests/host/core/{cloud_sync,cloud_state,cloud_outbox}_test.cpp`
- Produces: `enum class SyncAction : u8 { Nothing, Download, Upload, SetBase, Conflict }`;
  `struct SyncInput { std::optional<std::string> localHash; std::optional<SyncBase> base; std::optional<CloudEntry> cloud; }`
  mit `struct SyncBase { std::string hash, rev; }` (localHash nullopt = Einheit lokal leer);
  `SyncAction decideSync(const SyncInput&)` – genau die Spec-Tabelle; beide fehlen → Nothing.
  `class CloudState` über `IniDoc`: Abschnitt `[<anbieter>:<konto>/<einheit>]`, Schlüssel `hash`, `rev`, `played`
  (Unix-Zeit der letzten Sitzung); `std::optional<SyncBase> base(acct, unit)`, `void setBase(acct, unit, SyncBase)`,
  `void setPlayed(acct, unit, u64)`, `u64 played(acct, unit)`, `std::string serialize()`, `static CloudState parse(sv)`.
  `struct SessionEntry { std::string gameKey, unit; u64 started = 0; }`; `cloud_outbox`:
  `std::string serializeSession(const SessionEntry&)`, `std::optional<SessionEntry> parseSession(sv)`,
  `std::vector<std::string> parsePending(sv)` / `std::string serializePending(std::span<const std::string>)`
  (Spielschlüssel, eindeutig, Reihenfolge bleibt), `void addPending(std::vector<std::string>&, std::string key)`.
- [ ] Tests (FAIL): je Tabellenzeile ein Fall, dazu „fehlt lokal, Cloud da“ → Download, „lokal da, Cloud fehlt,
      Basis da“ → Upload, gleicher Hash bei beiden neu → SetBase; CloudState-Rundlauf mit zwei Konten getrennt;
      Session-Rundlauf, kaputte Session → nullopt; `addPending` doppelt → ein Eintrag.
- [ ] Umsetzen, Commit `feat: Abgleich-Entscheidung, Zustand und Outbox`.

### Task 8: Ablauf vor dem Start und nach dem Spielen
**Files:** `src/core/cloud_plan.{hpp,cpp}`, `tests/host/core/cloud_plan_test.cpp`, `tests/host/support/fake_provider.hpp`
- Consumes: Tasks 6–7, `readUnit`, `contentHash`, `backupUnit`, `restoreUnit`, `packSave`, `unpackSave`, `FileSystem`.
- Produces: `struct CloudCtx { FileSystem& fs; std::string dataDir; CloudProvider& provider; std::string account; std::string device; u64 now; std::function<void(std::string)> log; };`
  `enum class PreLaunch : u8 { Ready, Pulled, Pushed, Conflict, Skipped }`;
  `struct PreLaunchResult { PreLaunch kind; CloudEntry cloud; u64 localPlayed = 0; std::string reason; }`;
  `PreLaunchResult preLaunchSync(CloudCtx&, const SaveUnit&, std::string_view title, const volatile bool& cancel)`;
  `enum class ConflictChoice : u8 { KeepWii, TakeCloud }`;
  `PreLaunchResult resolveConflict(CloudCtx&, const SaveUnit&, std::string_view title, ConflictChoice, const volatile bool&)`;
  `CloudStatus uploadIfChanged(CloudCtx&, const SaveUnit&, std::string_view title, const volatile bool&)`
  (Ok auch, wenn nichts zu tun war). `CloudState` liest/schreibt `<data>/cloud/state.ini` selbst (atomar).
- Ablauf: lokaler Hash (`readUnit` + `contentHash`, leer → nullopt) → `stat` → `decideSync`.
  Download: `download`, `unpackSave`; Meta `unit` ≠ Einheit oder `contentHash` ≠ `cloud.hash` → Skipped
  (`reason = "error"`), nichts geschrieben; sonst `restoreUnit` (Sicherung + Journal aus M26) → Basis = (cloud.hash, cloud.rev).
  Upload: `packSave` (Meta mit `device`, `saved = now`), `upload(expectRev = base.rev oder leer)`; RevMismatch → einmal
  neu `stat` + `decideSync` (höchstens 2 Runden, dann Conflict). Unauthorized → Skipped „auth“, Offline → „offline“.
  Log nur mit Einheit und Ergebnis, nie mit Token oder Datei-ID.
- [ ] Tests (FAIL, FakeProvider im Speicher + MemFileSystem): erster Abgleich nur lokal → Upload, Basis gesetzt;
      zweiter Lauf ohne Änderung → Ready ohne Upload; Cloud neuer → Pulled, Datei hat Cloud-Bytes, Sicherung existiert;
      beide neu → Conflict mit `cloud.device`/`saved` und `localPlayed`; `resolveConflict(KeepWii)` lädt mit
      erwarteter Rev hoch, `TakeCloud` spielt zurück; Paket fremder Einheit → Skipped, Datei unverändert;
      Hash passt nicht zu `appProperties.hash` → Skipped; RevMismatch beim Upload und Cloud danach neu → Conflict;
      Offline → Skipped „offline“; im erfassten Log kommt kein Token-String des FakeProviders vor.
- [ ] Umsetzen, Commit `feat: Cloud-Abgleich vor dem Start und Upload danach`.

### Task 9: Netz-Warteschlange vorne, Cloud-Worker, Plattform-Hilfen
**Files:** `src/core/job_queue.{hpp,cpp}`, `tests/host/core/job_queue_test.cpp`, `src/platform/net_service.{hpp,cpp}`,
`src/platform/cloud_worker.{hpp,cpp}`, `src/platform/device_key.{hpp,cpp}`
- Produces: `u32 JobQueue::pushFront(NetJobFn)`; `u32 NetService::submitFront(NetJobFn)`;
  `std::vector<u8> deviceKeyMaterial()` (ES-Geräte-ID 4 Bytes + Konsolen-ID aus `sysinfo`, in Dolphin fest);
  `bool secureRandom(std::span<u8>)` (nur Netz-Thread, nur `NL_TLS`, `psa_generate_random`).
  `class CloudWorker` nach `NetWorker`-Muster (Mutex, `takeLogLines`, `running()`, `cancel()`, `join()`):
  `bool startLink(OAuthEndpoints)` – Gerätecode holen, Code für die UI bereitstellen (`std::optional<DeviceCode> deviceCode()`),
  im Intervall pollen, bei Ok Konto lesen, `tokens.bin` versiegeln (neues salt/iv aus `secureRandom`), `NL_CLOUD_LINKED drive`;
  `bool startPreLaunch(SaveUnit, std::string title)` über `submitFront`; `std::optional<PreLaunchResult> takePreLaunch()`;
  `bool startResolve(SaveUnit, std::string title, ConflictChoice)`; `bool startUploads(std::vector<std::pair<std::string, SaveUnit>>)`
  (Schlüssel + Einheit; gibt die bestätigten Schlüssel über `std::vector<std::string> takeUploaded()` zurück).
  Token-Datei wird beim ersten Auftrag gelesen und nach `tokensChanged()` neu versiegelt.
- [ ] Test (FAIL): `pushFront` überholt wartende Aufträge, nicht den laufenden; IDs steigen weiter streng.
- [ ] Umsetzen; Wii-Build mit und ohne TLS (ohne TLS: `CloudWorker` lehnt jeden Start ab, 0 Warnungen).
      Commit `feat: Cloud-Worker im Netz-Thread`.

### Task 10: Konfiguration und Oberfläche
**Files:** `src/core/config.{hpp,cpp}`, `src/core/settings_model.{hpp,cpp}`, `src/ui/cloud_scene.{hpp,cpp}`,
`src/ui/device_code_scene.{hpp,cpp}`, `src/ui/cloud_sync_scene.{hpp,cpp}`, `src/ui/strings.{hpp,cpp}`, `src/ui/scene.hpp`
(AppCommand), Tests `tests/host/core/config_test.cpp`, `tests/host/ui/{cloud_scene,device_code_scene,cloud_sync_scene}_test.cpp`
- Produces: `Config::cloudProvider` (`enum class CloudProviderId : u8 { None, Drive }`), `driveClientId`,
  `driveClientSecret`, Debug `debugCloudBase`, `debugCloudDeviceAuto`, `debugCloudConflict` (`""|wii|cloud|cancel`),
  `debugCloudSync` (Spielschlüssel); `SettingRow::Cloud` (Aktion) vor `Parental`.
  `CloudScene` (Zeilen: Anbieter Aus/Google Drive, Status „Nicht angemeldet / Angemeldet / Neu anmelden / Kein TLS /
  Keine Client-ID“, Anmelden); `AppCommand::CloudLink`, `AppCommand::CloudSkip`; `DeviceCodeScene` zeigt
  `verifyUrl`, `userCode` groß, Restzeit `mm:ss`, B bricht ab; `CloudSyncScene` mit Text je Phase und
  „B: Ohne Abgleich spielen“ (`AppCommand::CloudSkip`). Konflikt: `ChoiceDialog` mit drei Knöpfen
  „Wii-Stand behalten / Cloud-Stand laden / Abbrechen“ und Text „Wii: <Datum Uhrzeit> · Cloud: <Datum Uhrzeit>, <Gerät>“.
  Alle Texte DE + EN (`Str::Cloud…`).
- [ ] Tests (FAIL): `[cloud] provider = drive` und die IDs werden gelesen, unbekannter Wert → None, Secret erscheint
      nicht in `Config`-Debug-Ausgaben (falls vorhanden); `settingEnabled(Cloud)` false ohne TLS-Flag;
      CloudScene: ohne Client-ID ist „Anmelden“ ausgegraut; DeviceCodeScene: Restzeit 1799 s → „29:59“, B → Pop;
      CloudSyncScene: B → `CloudSkip`; Englisch liefert englische Texte.
- [ ] Umsetzen, Commit `feat: Cloud-Menü, Gerätecode- und Abgleich-Szene`.

### Task 11: Einbau in `main.cpp`
**Files:** `src/app/main.cpp`
- Consumes: Tasks 8–10, `saveQueryForGame`, `locateSave`, `ensureNand`, `ChoiceDialog`.
- Start-Tor nach den Preflight-Hinweisen (`warnedLaunch`, ca. `main.cpp:1239`), vor `recordStats`: wie `warnedLaunch`
  ein `cloudCleared`-Schlüssel. Nicht geklärt und Cloud aktiv und Einheit da → `startPreLaunch`, `CloudSyncScene`
  zeigen, `return`; im Frame-Pump: Ergebnis Ready/Pulled/Pushed/Skipped → `cloudCleared = key`, `launchByKey(key)`;
  Conflict → `ChoiceDialog`; Abbrechen → kein Start; 20 s ohne Ergebnis → `cancel`, `NL_CLOUD_SKIP timeout`, Start.
  NAND-Einheit und `!ensureNand()` → `NL_CLOUD_SKIP nand`, Start.
- Vor dem Abbau: `<data>/cloud/session.ini` (Schlüssel, Einheit, `time(nullptr)`) schreiben und `played` setzen.
- Beim Launcher-Start (nach `NL_SAVE_RECOVER`): `session.ini` vorhanden → Schlüssel nach `pending.ini`, Session
  löschen; Cloud aktiv → `startUploads` für alle `pending` mit erreichbarer Einheit (NAND gesperrt → bleibt);
  `takeUploaded` entfernt Einträge und schreibt `pending.ini` atomar.
- Debug: `cloud_base` → Basis für Auth und API; `cloud_device_auto = 1` → Anmeldung ohne Szene starten;
  `cloud_conflict` wählt den Knopf automatisch; `cloud_sync = <key>` führt das Tor für ein Spiel aus, ohne zu starten
  (Marker wie beim echten Tor, dann `NL_CLOUD_SYNC_DONE`).
- Ohne TLS: einmal `NL_CLOUD off notls`, kein Tor, keine Outbox-Uploads (Outbox wird trotzdem fortgeschrieben).
- [ ] Wii-Build mit und ohne TLS, 0 Warnungen; Host-Suite grün. Commit `feat: Cloud-Abgleich beim Spielstart`.

### Task 12: Fake-Server, Smokes, Doku, Abnahme
**Files:** `tools/cloud_fake.py`, `tools/tests/test_cloud_fake.py`, `tools/dolphin_smoke.py`, `tools/tests/test_dolphin.py`,
`docs/cloud-setup.md`, `docs/acceptance/m27-wii.md`, `docs/STATUS.md`
- `cloud_fake.py`: `ThreadingHTTPServer` + TLS-Kontext der Test-CA; Endpunkte aus Task 4/6 mit Zustand im Speicher
  (Dateien mit id, version, appProperties, Bytes); Gerätecode wird sofort bestätigt (`--approve auto`) oder nach dem
  ersten Poll; Optionen `--expire-access-once` (erste API-Anfrage 401), `--seed <einheit>=<zip>` (Cloud-Stand
  vorgeben), `--state-dump <datei>` (JSON beim Beenden, ohne Tokens). Antwortformen wie in Task 4/6.
- `dolphin_smoke.py --cloud-fake drive [--cloud-step link|push|pull|conflict|refresh|offline]` (erlaubt `--tls`
  implizit): schreibt `[cloud] provider = drive`, Fake-Client-ID, `[debug] cloud_base`, Marker je Schritt;
  `pull`/`conflict` nutzen die GB-Fixture aus `save_smoke` und `cloud_sync = <key>`; `push` legt `session.ini` an.
  Nach jedem Lauf: Log enthält keinen der Fake-Tokens (`ya29.fake…`, `1//fake…`) und kein Secret.
- Doku: `docs/cloud-setup.md` (Google Cloud Projekt, „TVs and Limited Input devices“, Scope `drive.file`, App auf
  „In production“, sonst verfallen Refresh-Tokens nach 7 Tagen; Einträge in `config.ini`), Abnahmeliste M27
  (Anmelden mit echtem Konto am Handy, Pull/Push/Konflikt mit Dolphin als zweitem Gerät, WLAN aus, Refresh nach > 1 h,
  Token nicht im Log), STATUS-Zeile.
- [ ] Tool-Tests (FAIL): Fake beantwortet Gerätecode, Token und Drive-Abfragen in Google-Form; 401 einmal;
      `--state-dump` ohne Tokens; Smoke-Verkabelung schreibt `[cloud]` und räumt `cloud/` hinterher auf; Log mit Token → Fehler.
- [ ] Smokes grün: link, push (`NL_CLOUD_PUSH ok`, Dump hat die Datei), pull (`NL_CLOUD_PULL ok`, Datei hat die
      Cloud-Bytes), conflict (`cloud_conflict=wii` → `NL_CLOUD_PUSH ok`), refresh (`NL_CLOUD_REFRESH ok`),
      offline (Server aus → `NL_CLOUD_SKIP offline`), ohne TLS-Build `NL_CLOUD off notls`.
- [ ] Commit `feat: Cloud-Fake-Server und Smokes`, `docs: Abnahme und Stand M27`.
