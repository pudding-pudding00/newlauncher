#pragma once
// Google Drive v3 als CloudProvider (M27, Spec Ziel 6). Scope drive.file, Ordner "newlauncher" im Stamm, je Einheit
// eine Datei "<einheit>.zip" mit appProperties (hash, saved, device, unit). Uploads bis 5 MiB multipart, darüber
// resumable mit einem einzigen PUT. Revision = Drive-Feld "version".
//
// Vor jeder Anfrage wird ein abgelaufenes Access-Token erneuert; auf 401 genau ein Refresh und eine Wiederholung.
// Scheitert der Refresh mit invalid_grant → Unauthorized ("Neu anmelden").

#include <functional>
#include <string>

#include "core/cloud_provider.hpp"
#include "core/http_types.hpp"
#include "core/oauth_client.hpp"
#include "core/oauth_device.hpp"

namespace nl {

class DriveProvider final : public CloudProvider {
public:
    DriveProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs,
                  std::string apiBase = "https://www.googleapis.com");

    CloudStatus account(std::string& id, const volatile bool& cancel) override;
    CloudStatus stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) override;
    CloudStatus download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) override;
    CloudStatus upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                       std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) override;

    bool tokensChanged() const override { return oauth_.tokensChanged(); }  // nach einem Refresh neu versiegeln
    bool refreshed() const override { return oauth_.refreshed(); }          // für NL_CLOUD_REFRESH

    static constexpr std::size_t kMultipartMax = 5 * 1024 * 1024;

private:
    HttpResponse call(HttpRequest req, const volatile bool& cancel) { return oauth_.call(std::move(req), cancel); }
    CloudStatus statusOf(const HttpResponse& r) const { return OAuthClient::classify(r, oauth_.authFail()); }
    CloudStatus folder(std::string& id, const volatile bool& cancel);

    OAuthClient oauth_;
    std::string api_;
    std::string folderId_;
};

}  // namespace nl
