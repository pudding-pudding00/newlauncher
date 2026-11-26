#include "core/oauth_device.hpp"

#include <utility>

#include "core/json.hpp"
#include "core/url_encode.hpp"

namespace nl {

namespace {

using Pair = std::pair<std::string_view, std::string_view>;

HttpResponse postForm(HttpTransport& http, const std::string& url, std::span<const Pair> form,
                      const volatile bool& cancel) {
    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = url;
    req.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    req.headers.emplace_back("Accept", "application/json");
    const std::string body = formEncode(form);
    req.body.assign(body.begin(), body.end());
    req.maxBody = 64 * 1024;
    return http.send(req, cancel);
}

// Antwort mit access_token in out übernehmen; false, wenn sie keins enthält.
bool takeTokens(const JsonValue& v, u64 nowMs, TokenSet& out) {
    const std::string_view access = v.string("access_token");
    if (access.empty()) return false;
    out.access = std::string(access);
    const std::string_view refresh = v.string("refresh_token");
    if (!refresh.empty()) out.refresh = std::string(refresh);
    const auto expires = v.integer("expires_in");
    out.expiresAtMs = nowMs + static_cast<u64>(expires && *expires > 0 ? *expires : 3600) * 1000;
    return true;
}

PollState stateFromError(std::string_view err) {
    if (err == "authorization_pending") return PollState::Pending;
    if (err == "slow_down") return PollState::SlowDown;
    if (err == "access_denied" || err == "invalid_grant") return PollState::Denied;
    if (err == "expired_token") return PollState::Expired;
    return PollState::Failed;
}

PollState tokenRequest(HttpTransport& http, const OAuthEndpoints& ep, std::span<const Pair> form, u64 nowMs,
                       TokenSet& out, const volatile bool& cancel) {
    const HttpResponse r = postForm(http, ep.tokenUrl, form, cancel);
    if (r.status == 0) return PollState::Failed;
    auto json = parseJson(r.text());
    if (!json.isOk()) return PollState::Failed;
    if (r.status == 200) return takeTokens(json.value(), nowMs, out) ? PollState::Ok : PollState::Failed;
    return stateFromError(json.value().string("error"));
}

}  // namespace

OAuthEndpoints googleEndpoints(std::string_view base, std::string_view clientId, std::string_view clientSecret) {
    const std::string b = base.empty() ? std::string("https://oauth2.googleapis.com") : std::string(base);
    OAuthEndpoints ep;
    ep.deviceUrl = b + "/device/code";
    ep.tokenUrl = b + "/token";
    ep.clientId = std::string(clientId);
    ep.clientSecret = std::string(clientSecret);
    ep.scope = std::string(kDriveScope);
    ep.revokeUrl = b + "/revoke";
    return ep;
}

RevokeResult revokeToken(HttpTransport& http, const OAuthEndpoints& ep, std::string_view token,
                         const volatile bool& cancel) {
    if (ep.revokeUrl.empty()) return RevokeResult::Unsupported;
    if (token.empty()) return RevokeResult::Failed;
    const Pair form[] = {{"token", token}};
    const HttpResponse r = postForm(http, ep.revokeUrl, form, cancel);
    return r.status == 200 ? RevokeResult::Ok : RevokeResult::Failed;
}

Result<DeviceCode> requestDeviceCode(HttpTransport& http, const OAuthEndpoints& ep, const volatile bool& cancel) {
    const Pair form[] = {{"client_id", ep.clientId}, {"scope", ep.scope}};
    const HttpResponse r = postForm(http, ep.deviceUrl, form, cancel);
    if (r.status != 200) {
        return Result<DeviceCode>::err(Error{ErrorCode::IoError, r.status == 0 ? "offline" : "status"});
    }
    auto json = parseJson(r.text());
    if (!json.isOk()) return Result<DeviceCode>::err(Error{ErrorCode::InvalidData, "json"});
    const JsonValue& v = json.value();
    DeviceCode dc;
    dc.deviceCode = std::string(v.string("device_code"));
    dc.userCode = std::string(v.string("user_code"));
    dc.verifyUrl = std::string(v.string("verification_url"));
    if (dc.verifyUrl.empty()) dc.verifyUrl = std::string(v.string("verification_uri"));
    const auto expires = v.integer("expires_in");
    const auto interval = v.integer("interval");
    dc.expiresInS = expires && *expires > 0 ? static_cast<u32>(*expires) : 0;
    dc.intervalS = interval && *interval > 0 ? static_cast<u32>(*interval) : 5;
    if (dc.deviceCode.empty() || dc.userCode.empty() || dc.verifyUrl.empty() || dc.expiresInS == 0) {
        return Result<DeviceCode>::err(Error{ErrorCode::InvalidData, "unvollständig"});
    }
    return Result<DeviceCode>::ok(std::move(dc));
}

PollState pollToken(HttpTransport& http, const OAuthEndpoints& ep, std::string_view deviceCode, u64 nowMs,
                    TokenSet& out, const volatile bool& cancel) {
    const Pair form[] = {{"client_id", ep.clientId},
                         {"client_secret", ep.clientSecret},
                         {"device_code", deviceCode},
                         {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}};
    const std::span<const Pair> all(form);
    // Ohne Secret (öffentliche Clients wie Microsoft) das Feld weglassen.
    if (ep.clientSecret.empty()) {
        const Pair pub[] = {form[0], form[2], form[3]};
        return tokenRequest(http, ep, pub, nowMs, out, cancel);
    }
    return tokenRequest(http, ep, all, nowMs, out, cancel);
}

PollState refreshToken(HttpTransport& http, const OAuthEndpoints& ep, TokenSet& io, u64 nowMs,
                       const volatile bool& cancel) {
    if (io.refresh.empty()) return PollState::Denied;
    const Pair form[] = {{"client_id", ep.clientId},
                         {"client_secret", ep.clientSecret},
                         {"refresh_token", io.refresh},
                         {"grant_type", "refresh_token"}};
    if (ep.clientSecret.empty()) {
        const Pair pub[] = {form[0], form[2], form[3]};
        return tokenRequest(http, ep, pub, nowMs, io, cancel);
    }
    return tokenRequest(http, ep, form, nowMs, io, cancel);
}

PollState exchangeAuthCode(HttpTransport& http, const OAuthEndpoints& ep, std::string_view code,
                           std::string_view verifier, u64 nowMs, TokenSet& out, const volatile bool& cancel) {
    const Pair form[] = {{"client_id", ep.clientId},
                         {"code", code},
                         {"code_verifier", verifier},
                         {"grant_type", "authorization_code"}};
    TokenSet t;
    const PollState s = tokenRequest(http, ep, form, nowMs, t, cancel);
    if (s == PollState::Ok) out = std::move(t);
    // Pending/SlowDown/Expired kennt der Code-Tausch nicht: alles außer invalid_grant ist ein Fehler.
    return s == PollState::Ok || s == PollState::Denied ? s : PollState::Failed;
}

bool tokenFresh(const TokenSet& t, u64 nowMs) {
    return !t.access.empty() && t.expiresAtMs != 0 && nowMs + 60000 < t.expiresAtMs;
}

}  // namespace nl
