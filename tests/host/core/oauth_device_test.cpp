#include "core/oauth_device.hpp"

#include <doctest/doctest.h>

#include <string>

#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;
using nl::test::offlineResponse;

namespace {
const bool kNoCancel = false;
OAuthEndpoints ep() { return googleEndpoints("https://auth.test", "cid.apps", "sec"); }
std::string bodyOf(const HttpRequest& r) { return std::string(r.body.begin(), r.body.end()); }
}  // namespace

TEST_CASE("googleEndpoints: Standard und eigene Basis") {
    const auto g = googleEndpoints("", "a", "b");
    CHECK(g.deviceUrl == "https://oauth2.googleapis.com/device/code");
    CHECK(g.tokenUrl == "https://oauth2.googleapis.com/token");
    CHECK(g.scope == kDriveScope);
    CHECK(ep().tokenUrl == "https://auth.test/token");
}

TEST_CASE("requestDeviceCode: Google-Antwort") {
    FakeTransport t;
    t.expect(HttpMethod::Post, "https://auth.test/device/code",
             jsonResponse(200, R"({"device_code":"DC1","user_code":"ABCD-EFGH",)"
                               R"("verification_url":"https://www.google.com/device","expires_in":1800,"interval":5})"));
    const auto r = requestDeviceCode(t, ep(), kNoCancel);
    REQUIRE(r.isOk());
    CHECK(r.value().deviceCode == "DC1");
    CHECK(r.value().userCode == "ABCD-EFGH");
    CHECK(r.value().verifyUrl == "https://www.google.com/device");
    CHECK(r.value().expiresInS == 1800);
    CHECK(r.value().intervalS == 5);
    const std::string body = bodyOf(t.seen[0]);
    CHECK(body.find("client_id=cid.apps") != std::string::npos);
    CHECK(body.find("scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fdrive.file") != std::string::npos);
    CHECK(t.seen[0].headers.size() >= 1);
}

TEST_CASE("requestDeviceCode: Microsoft-Form und fehlendes Intervall") {
    FakeTransport t;
    t.expect(HttpMethod::Post, "https://auth.test/device/code",
             jsonResponse(200, R"({"device_code":"D","user_code":"U",)"
                               R"("verification_uri":"https://microsoft.com/devicelogin","expires_in":900})"));
    const auto r = requestDeviceCode(t, ep(), kNoCancel);
    REQUIRE(r.isOk());
    CHECK(r.value().verifyUrl == "https://microsoft.com/devicelogin");
    CHECK(r.value().intervalS == 5);
}

TEST_CASE("requestDeviceCode: Fehler und offline") {
    FakeTransport t;
    t.expect(HttpMethod::Post, "https://auth.test/device/code", jsonResponse(401, R"({"error":"invalid_client"})"));
    t.expect(HttpMethod::Post, "https://auth.test/device/code", offlineResponse());
    t.expect(HttpMethod::Post, "https://auth.test/device/code", jsonResponse(200, R"({"user_code":"U"})"));
    CHECK_FALSE(requestDeviceCode(t, ep(), kNoCancel).isOk());
    CHECK_FALSE(requestDeviceCode(t, ep(), kNoCancel).isOk());
    CHECK_FALSE(requestDeviceCode(t, ep(), kNoCancel).isOk());  // device_code fehlt
}

TEST_CASE("pollToken: alle Zustände") {
    FakeTransport t;
    t.expect(HttpMethod::Post, "https://auth.test/token", jsonResponse(428, R"({"error":"authorization_pending"})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", jsonResponse(403, R"({"error":"slow_down"})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", jsonResponse(403, R"({"error":"access_denied"})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", jsonResponse(400, R"({"error":"expired_token"})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", offlineResponse());
    t.expect(HttpMethod::Post, "https://auth.test/token",
             jsonResponse(200, R"({"access_token":"ya29.A","expires_in":3599,"refresh_token":"1//R",)"
                               R"("scope":"https://www.googleapis.com/auth/drive.file","token_type":"Bearer"})"));
    TokenSet ts;
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::Pending);
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::SlowDown);
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::Denied);
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::Expired);
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::Failed);
    CHECK(ts.access.empty());
    CHECK(pollToken(t, ep(), "DC1", 1000, ts, kNoCancel) == PollState::Ok);
    CHECK(ts.access == "ya29.A");
    CHECK(ts.refresh == "1//R");
    CHECK(ts.expiresAtMs == 1000 + 3599000);
    const std::string body = bodyOf(t.seen[0]);
    CHECK(body.find("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code") != std::string::npos);
    CHECK(body.find("device_code=DC1") != std::string::npos);
    CHECK(body.find("client_secret=sec") != std::string::npos);
}

TEST_CASE("refreshToken: behält altes refresh_token, invalid_grant → Denied") {
    FakeTransport t;
    t.expect(HttpMethod::Post, "https://auth.test/token",
             jsonResponse(200, R"({"access_token":"ya29.B","expires_in":3600})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", jsonResponse(400, R"({"error":"invalid_grant"})"));
    t.expect(HttpMethod::Post, "https://auth.test/token", offlineResponse());
    TokenSet ts{"ya29.A", "1//R", 5};
    CHECK(refreshToken(t, ep(), ts, 2000, kNoCancel) == PollState::Ok);
    CHECK(ts.access == "ya29.B");
    CHECK(ts.refresh == "1//R");
    CHECK(ts.expiresAtMs == 2000 + 3600000);
    CHECK(bodyOf(t.seen[0]).find("grant_type=refresh_token") != std::string::npos);
    CHECK(bodyOf(t.seen[0]).find("refresh_token=1%2F%2FR") != std::string::npos);
    CHECK(refreshToken(t, ep(), ts, 3000, kNoCancel) == PollState::Denied);
    CHECK(refreshToken(t, ep(), ts, 3000, kNoCancel) == PollState::Failed);
    CHECK(ts.access == "ya29.B");
}

TEST_CASE("tokenFresh: 60 s Puffer") {
    TokenSet ts{"a", "r", 100000};
    CHECK(tokenFresh(ts, 39999));
    CHECK_FALSE(tokenFresh(ts, 41000));
    CHECK_FALSE(tokenFresh(TokenSet{"a", "r", 0}, 0));
    CHECK_FALSE(tokenFresh(TokenSet{"", "r", 100000}, 0));
}
