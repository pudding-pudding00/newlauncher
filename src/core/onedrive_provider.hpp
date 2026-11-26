#pragma once
// OneDrive (Microsoft Graph v1.0) als CloudProvider (M28, Spec Ziel 6). Scope Files.ReadWrite.AppFolder, App-Ordner
// /me/drive/special/approot, je Einheit eine Datei "<einheit>.zip". Revision = cTag ohne Anführungszeichen (ändert
// sich nur mit dem Inhalt, nicht mit der Beschreibung). Die Meta steht in "description":
//   nl1 hash=<hex> saved=<unix> device=<percent-kodiert>
// Upload bis 4 MiB mit einem PUT, darüber über eine Upload-Session mit einem einzigen PUT. Erwartete Revision →
// If-Match, sonst conflictBehavior=fail; 409/412 → RevMismatch. Downloads kommen als 302 auf eine vorab
// signierte URL; der Abruf dort geht ohne Authorization und nur über https.
#include <functional>
#include <string>

#include "core/cloud_provider.hpp"
#include "core/http_types.hpp"
#include "core/oauth_client.hpp"
#include "core/oauth_device.hpp"

namespace nl {

inline constexpr std::string_view kOneDriveScope = "Files.ReadWrite.AppFolder offline_access";

// base leer → https://login.microsoftonline.com; Endpunkte unter /consumers/oauth2/v2.0 (nur Privatkonten).
// Öffentlicher Client: kein Secret.
OAuthEndpoints microsoftEndpoints(std::string_view base, std::string_view clientId);

class OneDriveProvider final : public CloudProvider {
public:
    OneDriveProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs,
                     std::string apiBase = "https://graph.microsoft.com");

    CloudStatus account(std::string& id, const volatile bool& cancel) override;
    CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) override;
    CloudStatus download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) override;
    CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                       std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) override;

    bool tokensChanged() const override { return oauth_.tokensChanged(); }
    bool refreshed() const override { return oauth_.refreshed(); }

    static constexpr std::size_t kSimpleMax = 4 * 1024 * 1024;

private:
    CloudStatus statusOf(const HttpResponse& r) const { return OAuthClient::classify(r, oauth_.authFail()); }
    std::string itemPath(std::string_view unit) const;

    OAuthClient oauth_;
    std::string api_;
};

// Für Tests und den Fake-Server-Abgleich: description schreiben/lesen. parse → false, wenn kein "nl1"-Text.
std::string oneDriveDescription(const SaveMeta& meta);
bool parseOneDriveDescription(std::string_view text, CloudEntry& out);

}  // namespace nl
