#include "core/oauth_client.hpp"

#include <doctest/doctest.h>

#include <string>

#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;
using nl::test::offlineResponse;

namespace {

constexpr const char* kTokenUrl = "https://oauth.test/token";
constexpr const char* kRefreshOk = R"({"access_token":"new-access","expires_in":3600})";

struct Rig {
    FakeTransport http;
    TokenSet tokens{"old-access", "refresh-1", 10'000'000};
    u64 now = 1'000;
    OAuthClient client{http, googleEndpoints("https://oauth.test", "cid", "sec"), tokens, [this] { return now; }};
    volatile bool cancel = false;
};

HttpRequest get(std::string url) {
    HttpRequest r;
    r.method = HttpMethod::Get;
    r.url = std::move(url);
    return r;
}

std::string header(const HttpRequest& r, std::string_view name) {
    for (const auto& [k, v] : r.headers) {
        if (k == name) return v;
    }
    return {};
}

bool hasHeader(const HttpRequest& r, std::string_view name) {
    for (const auto& kv : r.headers) {
        if (kv.first == name) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("oauth_client: frisches Token geht direkt mit Bearer raus") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://api.test/x", jsonResponse(200, "{}"));
    const HttpResponse resp = r.client.call(get("https://api.test/x"), r.cancel);
    CHECK(resp.status == 200);
    CHECK(header(r.http.seen.at(0), "Authorization") == "Bearer old-access");
    CHECK(r.client.authFail() == CloudStatus::Ok);
    CHECK_FALSE(r.client.refreshed());
}

TEST_CASE("oauth_client: abgelaufenes Token wird genau einmal vor der Anfrage erneuert") {
    Rig r;
    r.tokens.expiresAtMs = 0;
    r.http.expect(HttpMethod::Post, kTokenUrl, jsonResponse(200, kRefreshOk));
    r.http.expect(HttpMethod::Get, "https://api.test/x", jsonResponse(200, "{}"));
    CHECK(r.client.call(get("https://api.test/x"), r.cancel).status == 200);
    CHECK(r.http.seen.size() == 2);
    CHECK(header(r.http.seen.at(1), "Authorization") == "Bearer new-access");
    CHECK(r.client.refreshed());
    CHECK(r.client.tokensChanged());
}

TEST_CASE("oauth_client: 401 → ein Refresh und eine Wiederholung, kein dritter Versuch") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://api.test/x", jsonResponse(401, "{}"));
    r.http.expect(HttpMethod::Post, kTokenUrl, jsonResponse(200, kRefreshOk));
    r.http.expect(HttpMethod::Get, "https://api.test/x", jsonResponse(401, "{}"));
    const HttpResponse resp = r.client.call(get("https://api.test/x"), r.cancel);
    CHECK(resp.status == 401);
    CHECK(r.http.seen.size() == 3);
    CHECK(r.http.done());
    CHECK(OAuthClient::classify(resp, r.client.authFail()) == CloudStatus::Unauthorized);
}

TEST_CASE("oauth_client: invalid_grant beim Refresh → Unauthorized ohne API-Anfrage") {
    Rig r;
    r.tokens.expiresAtMs = 0;
    r.http.expect(HttpMethod::Post, kTokenUrl, jsonResponse(400, R"({"error":"invalid_grant"})"));
    const HttpResponse resp = r.client.call(get("https://api.test/x"), r.cancel);
    CHECK(r.http.seen.size() == 1);
    CHECK(r.client.authFail() == CloudStatus::Unauthorized);
    CHECK(OAuthClient::classify(resp, r.client.authFail()) == CloudStatus::Unauthorized);
}

TEST_CASE("oauth_client: Refresh offline → Offline") {
    Rig r;
    r.tokens.expiresAtMs = 0;
    r.http.expect(HttpMethod::Post, kTokenUrl, offlineResponse());
    r.client.call(get("https://api.test/x"), r.cancel);
    CHECK(r.client.authFail() == CloudStatus::Offline);
}

TEST_CASE("oauth_client: plain sendet nie einen Authorization-Header") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://cdn.test/blob", jsonResponse(200, "x"));
    HttpRequest req = get("https://cdn.test/blob");
    req.headers.emplace_back("Authorization", "Bearer leak");
    CHECK(r.client.plain(req, r.cancel).status == 200);
    CHECK_FALSE(hasHeader(r.http.seen.at(0), "Authorization"));
    CHECK(r.client.authFail() == CloudStatus::Ok);
}

TEST_CASE("oauth_client: classify-Tabelle") {
    auto st = [](u16 s) { return jsonResponse(s, "{}"); };
    CHECK(OAuthClient::classify(st(200), CloudStatus::Ok) == CloudStatus::Ok);
    CHECK(OAuthClient::classify(st(201), CloudStatus::Ok) == CloudStatus::Ok);
    CHECK(OAuthClient::classify(st(204), CloudStatus::Ok) == CloudStatus::Ok);
    CHECK(OAuthClient::classify(st(401), CloudStatus::Ok) == CloudStatus::Unauthorized);
    CHECK(OAuthClient::classify(st(404), CloudStatus::Ok) == CloudStatus::NotFound);
    CHECK(OAuthClient::classify(st(409), CloudStatus::Ok) == CloudStatus::RevMismatch);
    CHECK(OAuthClient::classify(st(412), CloudStatus::Ok) == CloudStatus::RevMismatch);
    CHECK(OAuthClient::classify(st(500), CloudStatus::Ok) == CloudStatus::Failed);
    CHECK(OAuthClient::classify(offlineResponse(), CloudStatus::Ok) == CloudStatus::Offline);
    CHECK(OAuthClient::classify(st(200), CloudStatus::Offline) == CloudStatus::Offline);  // Refresh-Fehler gewinnt
}
