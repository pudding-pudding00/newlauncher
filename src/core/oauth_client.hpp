#pragma once
// OAuth-Hülle für alle Cloud-Anbieter (M28, aus DriveProvider gelöst). Vor jeder Anfrage wird ein abgelaufenes
// Access-Token erneuert; auf 401 folgen genau ein Refresh und eine Wiederholung. Scheitert der Refresh, liefert
// call() eine leere Antwort und authFail() sagt warum (Unauthorized bei invalid_grant, sonst Offline/Failed).
// plain() sendet ohne Authorization-Header: für Umleitungs- und Upload-URLs fremder Hosts.
#include <functional>

#include "core/cloud_provider.hpp"
#include "core/http_types.hpp"
#include "core/oauth_device.hpp"

namespace nl {

class OAuthClient {
public:
    OAuthClient(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs);

    HttpResponse call(HttpRequest req, const volatile bool& cancel);
    HttpResponse plain(HttpRequest req, const volatile bool& cancel);

    CloudStatus authFail() const { return authFail_; }  // Ergebnis eines gescheiterten Refresh im letzten call()
    bool tokensChanged() const { return tokensChanged_; }
    bool refreshed() const { return refreshed_; }

    // authFail != Ok gewinnt; Status 0 → Offline (Netzfehler) bzw. Failed; 2xx Ok, 401 Unauthorized, 404 NotFound,
    // 409/412 RevMismatch, sonst Failed.
    static CloudStatus classify(const HttpResponse& r, CloudStatus authFail);

private:
    // Leitet weiter und merkt sich die letzte Antwort, damit ein Refresh offline von abgelehnt trennbar ist.
    struct Tap final : HttpTransport {
        explicit Tap(HttpTransport& inner) : inner(inner) {}
        HttpResponse send(const HttpRequest& req, const volatile bool& cancel) override;
        HttpTransport& inner;
        u16 lastStatus = 0;
        NetError lastError = NetError::None;
    };

    CloudStatus refresh(const volatile bool& cancel);

    Tap http_;
    OAuthEndpoints ep_;
    TokenSet& tokens_;
    std::function<u64()> nowMs_;
    CloudStatus authFail_ = CloudStatus::Ok;
    bool tokensChanged_ = false;
    bool refreshed_ = false;
};

bool offlineNetError(NetError e);  // Verbindungsfehler (nicht Speicher/sonstiges)

}  // namespace nl
