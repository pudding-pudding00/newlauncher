#pragma once
// HTTP-GET über die IOS-Sockets (Spec 6.4), mit NL_TLS auch HTTPS über wii_tls (M11, Spec v2 4.2). Nur im Netz-Thread
// (NetService) aufrufen; dort laufen net_init und wtls_init, und nur dort dürfen wtls_*-Aufrufe stehen.

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/http.hpp"
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

// Verbindung: Klartext-Socket oder TLS-Sitzung. send/recv warten selbst (Timeouts, Abbruch etwa alle 100 ms).
// send: >0 gesendete Bytes, <0 Fehler. recv: >0 Bytes, 0 = sauberes Ende, <0 Fehler. Nach einem Fehler nennt error()
// die Ursache.
class Transport {
public:
    virtual ~Transport() = default;
    virtual s32 send(std::span<const u8> data) = 0;
    virtual s32 recv(std::span<u8> buf) = 0;
    virtual void close() = 0;
    NetError error() const { return error_; }

protected:
    NetError error_ = NetError::None;
};

// Pfad einer zusätzlichen Test-CA ([debug] tls_ca, "sd:/…"); leer = nur das eingebaute Mozilla-Bundle. Vom
// Haupt-Thread vor dem ersten NetService::submit setzen; geladen wird beim ersten HTTPS-Auftrag (Log NL_TLS_CA).
void setTlsCaPath(std::string path);

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

}  // namespace nl::platform
