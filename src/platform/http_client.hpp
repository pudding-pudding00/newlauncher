#pragma once
// HTTP/HTTPS-Client über wii_tls (<wiitls/http.h>, ab M11/v0.2.0 mit Keep-Alive-Pool, gzip und Weiterleitungen);
// ohne NL_TLS Klartext-Sockets mit core/http. Nur im Netz-Thread (NetService) aufrufen; dort laufen net_init und
// wtls_init, und nur dort dürfen wtls_*-Aufrufe stehen.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/http.hpp"
#include "core/http_types.hpp"
#include "core/net_error.hpp"
#include "core/types.hpp"

namespace nl::platform {

// outcome wie bisher; error nennt die Ursache (None bei Ok/NotFound, Http bei anderem Status); status = HTTP-Status
// oder 0, wenn keine Antwort kam.
struct HttpResult {
    HttpOutcome outcome = HttpOutcome::Failed;
    NetError error = NetError::Other;
    u16 status = 0;
};

// Marker an den Aufrufer (NL_NET_FAIL <name>, NL_TLS_CA, NL_TLS_TIMESYNC, NL_TLS_MISSING); muss threadsicher sein.
using HttpLog = std::function<void(std::string)>;

// Pfad einer zusätzlichen Test-CA ([debug] tls_ca, "sd:/…"); leer = nur das eingebaute Mozilla-Bundle. Vom
// Haupt-Thread vor dem ersten NetService::submit setzen; geladen wird beim ersten HTTPS-Auftrag (Log NL_TLS_CA).
void setTlsCaPath(std::string path);
// [debug] perf (Spec v2 4.6): je erfolgreichem TLS-Handshake "NL_PERF tls heap=<Bytes> ms=<Dauer>". Wie setTlsCaPath
// vor dem ersten submit setzen.
void setNetPerf(bool on);
bool netPerf();
// M23 (nur im Netz-Thread): kam die letzte httpGet-Antwort über eine wiederverwendete Keep-Alive-Verbindung, und
// wie lange dauerte ihr TLS-Handshake (0 bei Wiederverwendung, Klartext oder ohne NL_TLS)?
bool lastRequestReused();
u32 lastHandshakeMs();
// Stand für die Systeminfo (vom Haupt-Thread lesbar): true, sobald in dieser Sitzung die Uhr per SNTP gestellt
// wurde. Ohne NL_TLS immer false.
bool clockSynced();
// Einrichtungs-Assistent (M13): Uhr per SNTP stellen, falls in dieser Sitzung noch nicht geschehen (höchstens die
// zwei Versuche je Sitzung, die auch httpGet hat). Nur im Netz-Thread aufrufen. true = Uhr gestellt; ohne NL_TLS false.
bool syncClockOnce(const HttpLog& log = {});

// Ok nur bei Status 200 mit vollständigem Body (höchstens maxBody Bytes); 404 → NotFound; alles andere Failed.
// http:// über Klartext, https:// über TLS (ohne NL_TLS: Failed/TlsMissing, kein Rückfall auf HTTP). Klartext:
// Verbindungsaufbau, Senden und Empfangen je 10 s Timeout. TLS: 8 s Verbindung, 10 s Handshake, 15 s I/O; bei
// Uhr-Verdacht einmal SNTP und ein zweiter Versuch. cancel (optional) gesetzt → Failed. DNS und der TLS-Handshake
// sind nicht unterbrechbar.
HttpResult httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel = nullptr,
                   const HttpLog& log = {});

// Wie httpGet, schreibt den Body aber per fopen/fwrite (64-KB-Puffer) nach destFile ("sd:/…"). received zählt die
// Body-Bytes mit (für die Fortschrittsanzeige). Bei jedem Ergebnis außer Ok wird destFile gelöscht.
HttpResult httpDownload(const Url& url, const std::string& destFile, u32 maxBody, const volatile bool* cancel,
                        volatile u32* received, const HttpLog& log = {});

// M27: beliebige Methode mit eigenen Headern und Body, nur https:// (Test-CA, SNTP-Versuch und Timeouts wie
// httpGet). Folgt keiner Umleitung und liefert jeden Status unverändert mit Antwort-Headern und Body (höchstens
// request.maxBody Bytes). status 0 = keine vollständige Antwort; ohne NL_TLS immer {0, TlsMissing}.
HttpResponse httpSend(const HttpRequest& request, const volatile bool* cancel, const HttpLog& log = {});

// HttpTransport für core/ (Cloud-Anbieter); nur im Netz-Thread benutzen.
class WtlsTransport final : public HttpTransport {
public:
    explicit WtlsTransport(HttpLog log = {}) : log_(std::move(log)) {}
    HttpResponse send(const HttpRequest& req, const volatile bool& cancel) override;

private:
    HttpLog log_;
};

}  // namespace nl::platform
