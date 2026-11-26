#include "core/dropbox_provider.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/json.hpp"
#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;
using nl::test::offlineResponse;

namespace {

constexpr const char* kApi = "https://api.test";
constexpr const char* kContent = "https://content.test";

struct Rig {
    FakeTransport http;
    TokenSet tokens{"old-access", "refresh-1", 10'000'000};
    u64 now = 1'000;
    DropboxProvider box{http, dropboxEndpoints("https://auth.test", "appkey"), tokens, [this] { return now; }, kApi,
                        kContent};
    volatile bool cancel = false;
};

std::string header(const HttpRequest& r, std::string_view name) {
    for (const auto& [k, v] : r.headers) {
        if (k == name) return v;
    }
    return {};
}

std::string bodyText(const HttpRequest& r) { return {r.body.begin(), r.body.end()}; }

JsonValue apiArg(const HttpRequest& r) {
    auto v = parseJson(header(r, "Dropbox-API-Arg"));
    REQUIRE(v.isOk());
    return std::move(v.value());
}

SaveMeta meta() {
    SaveMeta m;
    m.unit = "wii-RMCP";
    m.hash = "ab12";
    m.device = "Wii";
    m.saved = 42;
    return m;
}

HttpResponse binary(u16 status, std::vector<u8> data) {
    HttpResponse r;
    r.status = status;
    r.error = NetError::None;
    r.body = std::move(data);
    return r;
}

constexpr const char* kFile =
    R"({".tag":"file","name":"wii-RMCP.zip","id":"id:a4ayc_80_OEAAAAAAAAAXw","rev":"a1c10ce0dd78","size":1234,)"
    R"("server_modified":"2026-11-26T10:00:00Z","content_hash":"e3b0"})";

}  // namespace

TEST_CASE("dropboxEndpoints und Autorisierungs-URL") {
    const OAuthEndpoints real = dropboxEndpoints("", "key1");
    CHECK(real.tokenUrl == "https://api.dropboxapi.com/oauth2/token");
    CHECK(real.clientId == "key1");
    CHECK(real.clientSecret.empty());
    CHECK(real.deviceUrl.empty());
    CHECK(real.revokeUrl.empty());  // Widerruf geht über DropboxProvider::revoke (Bearer), nicht per Formular
    CHECK(dropboxAuthorizeUrl(real, "CH-_1", "") ==
          "https://www.dropbox.com/oauth2/authorize?client_id=key1&response_type=code&code_challenge=CH-_1"
          "&code_challenge_method=S256&token_access_type=offline");
    const OAuthEndpoints fake = dropboxEndpoints("https://fake.test:8443", "k");
    CHECK(fake.tokenUrl == "https://fake.test:8443/oauth2/token");
    CHECK(dropboxAuthorizeUrl(fake, "c", "https://fake.test:8443")
              .rfind("https://fake.test:8443/oauth2/authorize?client_id=k&", 0) == 0);
}

TEST_CASE("DropboxProvider::account: account_id") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://api.test/2/users/get_current_account",
                  jsonResponse(200, R"({"account_id":"dbid:AAH4f99T0taONIb-OurWxbNQ6ywGRopQngc","email":"x@y"})"));
    std::string id;
    CHECK(r.box.account(id, r.cancel) == CloudStatus::Ok);
    CHECK(id == "dbid:AAH4f99T0taONIb-OurWxbNQ6ywGRopQngc");
    CHECK(bodyText(r.http.seen[0]) == "null");
    CHECK(header(r.http.seen[0], "Content-Type") == "application/json");
    CHECK(header(r.http.seen[0], "Authorization") == "Bearer old-access");
}

TEST_CASE("DropboxProvider::stat: Datei, ohne Meta") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata", jsonResponse(200, kFile));
    CloudEntry e;
    CHECK(r.box.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.id == "id:a4ayc_80_OEAAAAAAAAAXw");
    CHECK(e.rev == "a1c10ce0dd78");
    CHECK(e.size == 1234);
    CHECK(e.hash.empty());
    auto j = parseJson(bodyText(r.http.seen[0]));
    REQUIRE(j.isOk());
    CHECK(j.value().string("path") == "/wii-RMCP.zip");
}

TEST_CASE("DropboxProvider::stat: 409 path/not_found → NotFound, andere 409 → Failed") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata",
                  jsonResponse(409, R"({"error_summary":"path/not_found/..","error":{".tag":"path"}})"));
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata",
                  jsonResponse(409, R"({"error_summary":"path/restricted_content/.."})"));
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata",
                  jsonResponse(200, R"({".tag":"folder","name":"wii-RMCP.zip","id":"id:x"})"));
    CloudEntry e;
    CHECK(r.box.stat("wii-RMCP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(r.box.stat("wii-RMCP", e, r.cancel) == CloudStatus::Failed);
    CHECK(r.box.stat("wii-RMCP", e, r.cancel) == CloudStatus::Failed);
}

TEST_CASE("DropboxProvider::stat: unsichere Einheit ohne Anfrage") {
    Rig r;
    CloudEntry e;
    CHECK(r.box.stat("a/b", e, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.empty());
}

TEST_CASE("DropboxProvider: 401 → Refresh und eine Wiederholung") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata",
                  jsonResponse(401, R"({"error_summary":"expired_access_token/.."})"));
    r.http.expect(HttpMethod::Post, "https://auth.test/oauth2/token",
                  jsonResponse(200, R"({"access_token":"new-access","expires_in":14400})"));
    r.http.expect(HttpMethod::Post, "https://api.test/2/files/get_metadata", jsonResponse(200, kFile));
    CloudEntry e;
    CHECK(r.box.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(header(r.http.seen[2], "Authorization") == "Bearer new-access");
    CHECK(r.box.tokensChanged());
}

TEST_CASE("DropboxProvider::download: per Revision, Grenze") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/download", binary(200, {1, 2, 3}));
    CloudEntry e;
    e.id = "id:a4ayc_80_OEAAAAAAAAAXw";
    e.rev = "a1c10ce0dd78";
    std::vector<u8> zip;
    CHECK(r.box.download(e, zip, r.cancel) == CloudStatus::Ok);
    CHECK(zip == std::vector<u8>{1, 2, 3});
    CHECK(apiArg(r.http.seen[0]).string("path") == "rev:a1c10ce0dd78");
    CHECK(r.http.seen[0].maxBody == kMaxCloudPackageBytes);
    CHECK(r.http.seen[0].body.empty());

    e.rev = "a1c\"}";  // fremde Zeichen kommen nie in den Header
    CHECK(r.box.download(e, zip, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.size() == 1);
}

TEST_CASE("DropboxProvider::download: verschwundene Revision → Failed") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/download",
                  jsonResponse(409, R"({"error_summary":"path/not_found/.."})"));
    CloudEntry e;
    e.rev = "a1c10ce0dd78";
    std::vector<u8> zip;
    CHECK(r.box.download(e, zip, r.cancel) == CloudStatus::Failed);
}

TEST_CASE("DropboxProvider::upload: ohne Rev add, mit Rev update") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload", jsonResponse(200, kFile));
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload", jsonResponse(200, kFile));
    const std::vector<u8> zip{9, 8, 7};
    CloudEntry out;
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Ok);
    CHECK(out.rev == "a1c10ce0dd78");
    CHECK(out.hash == "ab12");  // aus der eigenen Meta, ohne Download
    CHECK(out.device == "Wii");
    CHECK(out.saved == 42);
    const JsonValue add = apiArg(r.http.seen[0]);
    CHECK(add.string("path") == "/wii-RMCP.zip");
    CHECK(add.string("mode") == "add");
    CHECK(add.get("autorename") != nullptr);
    CHECK(add.get("strict_conflict") != nullptr);
    CHECK(header(r.http.seen[0], "Content-Type") == "application/octet-stream");
    CHECK(r.http.seen[0].bodyView.size() + r.http.seen[0].body.size() == 3);

    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::Ok);
    const JsonValue upd = apiArg(r.http.seen[1]);
    const JsonValue* mode = upd.get("mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->string(".tag") == "update");
    CHECK(mode->string("update") == "a1c10ce0dd78");
}

TEST_CASE("DropboxProvider::upload: 409 path/conflict → RevMismatch, andere 409 → Failed") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload",
                  jsonResponse(409, R"({"error_summary":"path/conflict/file/..."})"));
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload",
                  jsonResponse(409, R"({"error_summary":"path/insufficient_space/.."})"));
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload",
                  jsonResponse(409, R"({"error_summary":"too_many_write_operations/.."})"));
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload",
                  jsonResponse(429, R"({"error_summary":"too_many_write_operations/.."})"));
    r.http.expect(HttpMethod::Post, "https://content.test/2/files/upload", offlineResponse());
    const std::vector<u8> zip{1};
    CloudEntry out;
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::Failed);
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::Failed);
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::Failed);
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "a1c10ce0dd78", out, r.cancel) == CloudStatus::Offline);
}

TEST_CASE("DropboxProvider::upload: zu groß oder unsichere Rev ohne Anfrage") {
    Rig r;
    std::vector<u8> big(kMaxCloudPackageBytes + 1);
    CloudEntry out;
    CHECK(r.box.upload("wii-RMCP", big, meta(), "", out, r.cancel) == CloudStatus::Failed);
    const std::vector<u8> zip{1};
    CHECK(r.box.upload("wii-RMCP", zip, meta(), "x\"y", out, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.empty());
}

TEST_CASE("DropboxProvider::revoke: Bearer ohne Body") {
    Rig r;
    r.http.expect(HttpMethod::Post, "https://api.test/2/auth/token/revoke", jsonResponse(200, "null"));
    r.http.expect(HttpMethod::Post, "https://api.test/2/auth/token/revoke", offlineResponse());
    CHECK(r.box.revoke(r.cancel) == RevokeResult::Ok);
    CHECK(header(r.http.seen[0], "Authorization") == "Bearer old-access");
    CHECK(r.http.seen[0].body.empty());
    CHECK(r.box.revoke(r.cancel) == RevokeResult::Failed);
}
