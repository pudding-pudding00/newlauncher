#pragma once
// Dropbox (API v2) als CloudProvider (M29, Spec Ziel 6). Zugriffsart „App folder“, je Einheit eine Datei
// "/<einheit>.zip" im App-Ordner. Revision = "rev". Dropbox kennt keine freie Beschreibung: hash bleibt leer, Gerät und
// Zeit kommen aus dem Paket (cloud_plan lädt es nur bei neuer Revision). Alle Endpunktfehler kommen als 409; die
// Abbildung geht über error_summary (path/not_found → NotFound, path/conflict → RevMismatch, sonst Failed).
// Anmelden per PKCE (core/pkce, exchangeAuthCode) ohne Umleitung; öffentlicher Client ohne Secret.
#include <functional>
#include <string>

#include "core/cloud_provider.hpp"
#include "core/http_types.hpp"
#include "core/oauth_client.hpp"
#include "core/oauth_device.hpp"

namespace nl {

// base leer → https://api.dropboxapi.com/oauth2/token; sonst base + "/oauth2/token" (Fake-Server). revokeUrl bleibt
// leer: der Widerruf braucht das Bearer-Token und läuft über DropboxProvider::revoke.
OAuthEndpoints dropboxEndpoints(std::string_view base, std::string_view appKey);
// Seite zum Zulassen; ohne redirect_uri zeigt Dropbox danach den Code an. base leer → https://www.dropbox.com.
std::string dropboxAuthorizeUrl(const OAuthEndpoints& ep, std::string_view challenge, std::string_view base);

class DropboxProvider final : public CloudProvider {
public:
    DropboxProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs,
                    std::string apiBase = "https://api.dropboxapi.com",
                    std::string contentBase = "https://content.dropboxapi.com");

    CloudStatus account(std::string& id, const volatile bool& cancel) override;
    CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) override;
    CloudStatus download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) override;
    CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                       std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) override;

    // Abmelden: widerruft das Access-Token und damit die ganze Freigabe (auch das Refresh-Token).
    RevokeResult revoke(const volatile bool& cancel);

    bool tokensChanged() const override { return oauth_.tokensChanged(); }
    bool refreshed() const override { return oauth_.refreshed(); }

private:
    CloudStatus statusOf(const HttpResponse& r) const;

    OAuthClient oauth_;
    std::string api_, content_;
};

}  // namespace nl
