#include "core/oauth_client.hpp"

#include <algorithm>
#include <utility>

namespace nl {

namespace {

void setBearer(HttpHeaders& h, const std::string& access) {
    h.erase(std::remove_if(h.begin(), h.end(), [](const auto& kv) { return kv.first == "Authorization"; }), h.end());
    h.emplace_back("Authorization", "Bearer " + access);
}

}  // namespace

bool offlineNetError(NetError e) { return e != NetError::Other && e != NetError::NoMem && e != NetError::None; }

HttpResponse OAuthClient::Tap::send(const HttpRequest& req, const volatile bool& cancel) {
    HttpResponse r = inner.send(req, cancel);
    lastStatus = r.status;
    lastError = r.error;
    return r;
}

OAuthClient::OAuthClient(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs)
    : http_(http), ep_(std::move(ep)), tokens_(tokens), nowMs_(std::move(nowMs)) {}

CloudStatus OAuthClient::refresh(const volatile bool& cancel) {
    http_.lastStatus = 0;
    http_.lastError = NetError::None;
    switch (refreshToken(http_, ep_, tokens_, nowMs_(), cancel)) {
        case PollState::Ok:
            tokensChanged_ = true;
            refreshed_ = true;
            return CloudStatus::Ok;
        case PollState::Denied: return CloudStatus::Unauthorized;
        default: break;
    }
    if (http_.lastStatus == 0 && offlineNetError(http_.lastError)) return CloudStatus::Offline;
    return CloudStatus::Failed;
}

HttpResponse OAuthClient::call(HttpRequest req, const volatile bool& cancel) {
    authFail_ = CloudStatus::Ok;
    bool refreshedHere = false;
    if (!tokenFresh(tokens_, nowMs_())) {
        const CloudStatus s = refresh(cancel);
        if (s != CloudStatus::Ok) {
            authFail_ = s;
            return HttpResponse{};
        }
        refreshedHere = true;
    }
    setBearer(req.headers, tokens_.access);
    HttpResponse r = http_.send(req, cancel);
    if (r.status != 401 || refreshedHere) return r;

    // Genau ein Refresh je Anfrage, dann eine Wiederholung.
    const CloudStatus s = refresh(cancel);
    if (s != CloudStatus::Ok) {
        authFail_ = s;
        return HttpResponse{};
    }
    setBearer(req.headers, tokens_.access);
    return http_.send(req, cancel);
}

HttpResponse OAuthClient::plain(HttpRequest req, const volatile bool& cancel) {
    authFail_ = CloudStatus::Ok;
    auto& h = req.headers;
    h.erase(std::remove_if(h.begin(), h.end(), [](const auto& kv) { return kv.first == "Authorization"; }), h.end());
    return http_.send(req, cancel);
}

CloudStatus OAuthClient::classify(const HttpResponse& r, CloudStatus authFail) {
    if (authFail != CloudStatus::Ok) return authFail;
    if (r.status == 0) return offlineNetError(r.error) ? CloudStatus::Offline : CloudStatus::Failed;
    if (r.status >= 200 && r.status < 300) return CloudStatus::Ok;
    if (r.status == 401) return CloudStatus::Unauthorized;
    if (r.status == 404) return CloudStatus::NotFound;
    if (r.status == 409 || r.status == 412) return CloudStatus::RevMismatch;
    return CloudStatus::Failed;
}

}  // namespace nl
