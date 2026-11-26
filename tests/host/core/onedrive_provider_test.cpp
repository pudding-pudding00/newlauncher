#include "core/onedrive_provider.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;

namespace {

constexpr const char* kApi = "https://graph.test";
constexpr const char* kItem = "https://graph.test/v1.0/me/drive/special/approot:/wii-RMCP.zip";

struct Rig {
    FakeTransport http;
    TokenSet tokens{"old-access", "refresh-1", 10'000'000};
    u64 now = 1'000;
    OneDriveProvider drive{http, microsoftEndpoints("https://login.test", "cid"), tokens, [this] { return now; }, kApi};
    volatile bool cancel = false;
};

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

std::string bodyText(const HttpRequest& r) { return {r.body.begin(), r.body.end()}; }

SaveMeta meta() {
    SaveMeta m;
    m.unit = "wii-RMCP";
    m.title = "Mario Kart";
    m.device = "Wii Küche";
    m.saved = 1'700'000'000;
    m.hash = std::string(64, 'a');
    return m;
}

HttpResponse redirect(std::string location) {
    HttpResponse r;
    r.status = 302;
    r.headers.emplace_back("Location", std::move(location));
    return r;
}

HttpResponse raw(u16 status, std::string_view body) {
    HttpResponse r;
    r.status = status;
    r.body.assign(body.begin(), body.end());
    return r;
}

const char* kItemJson = R"({"id":"ABC!12","cTag":"\"c:{X},3\"","size":4})";

}  // namespace

TEST_CASE("microsoftEndpoints: consumers, Scope, kein Secret") {
    const OAuthEndpoints ep = microsoftEndpoints("", "cid");
    CHECK(ep.deviceUrl == "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode");
    CHECK(ep.tokenUrl == "https://login.microsoftonline.com/consumers/oauth2/v2.0/token");
    CHECK(ep.scope == "Files.ReadWrite.AppFolder offline_access");
    CHECK(ep.clientSecret.empty());
    CHECK(microsoftEndpoints("https://login.test", "cid").tokenUrl == "https://login.test/consumers/oauth2/v2.0/token");
}

TEST_CASE("onedrive: Gerätecode mit Microsoft-Antwortform, Formular ohne client_secret") {
    FakeTransport http;
    http.expect(HttpMethod::Post, "https://login.test/consumers/oauth2/v2.0/devicecode",
                jsonResponse(200, R"({"device_code":"DC","user_code":"ABCD-EFGH",)"
                                  R"("verification_uri":"https://microsoft.com/devicelogin","expires_in":900,)"
                                  R"("interval":5,"message":"To sign in, use a web browser"})"));
    volatile bool cancel = false;
    auto dc = requestDeviceCode(http, microsoftEndpoints("https://login.test", "cid"), cancel);
    REQUIRE(dc.isOk());
    CHECK(dc.value().verifyUrl == "https://microsoft.com/devicelogin");
    CHECK(dc.value().userCode == "ABCD-EFGH");
    CHECK(bodyText(http.seen.at(0)).find("client_secret") == std::string::npos);
    CHECK(bodyText(http.seen.at(0)).find("scope=Files.ReadWrite.AppFolder%20offline_access") != std::string::npos);
}

TEST_CASE("onedrive: account liest die Laufwerks-ID mit Bearer") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://graph.test/v1.0/me/drive?$select=id",
                  jsonResponse(200, R"({"id":"b1c2d3e4f5a6"})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Ok);
    CHECK(id == "b1c2d3e4f5a6");
    CHECK(header(r.http.seen.at(0), "Authorization") == "Bearer old-access");
}

TEST_CASE("onedrive: account mit unsicherer ID → Failed") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://graph.test/v1.0/me/drive", jsonResponse(200, R"({"id":"a/b"})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Failed);
}

TEST_CASE("onedrive: stat liest cTag als Revision und die Meta aus description") {
    Rig r;
    r.http.expect(HttpMethod::Get, std::string(kItem) + "?$select=id,cTag,size,description",
                  jsonResponse(200, R"({"id":"ABC!12","cTag":"\"c:{X},3\"","size":1234,)"
                                    R"("description":"nl1 hash=abc saved=1700000000 device=Wii%20K%C3%BCche"})"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.id == "ABC!12");
    CHECK(e.rev == "c:{X},3");
    CHECK(e.size == 1234);
    CHECK(e.hash == "abc");
    CHECK(e.saved == 1'700'000'000);
    CHECK(e.device == "Wii Küche");
}

TEST_CASE("onedrive: stat ohne oder mit fremder description → Eintrag ohne Hash") {
    Rig r;
    r.http.expect(HttpMethod::Get, kItem, jsonResponse(200, R"({"id":"ABC!12","cTag":"\"c:{X},3\"","size":5})"));
    r.http.expect(HttpMethod::Get, kItem,
                  jsonResponse(200, R"({"id":"ABC!12","cTag":"\"c:{X},3\"","description":"Urlaubsfotos"})"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.hash.empty());
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.hash.empty());
}

TEST_CASE("onedrive: stat 404 → NotFound; unsichere Einheit → Failed ohne Anfrage") {
    Rig r;
    r.http.expect(HttpMethod::Get, kItem, jsonResponse(404, R"({"error":{"code":"itemNotFound"}})"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(r.drive.stat("../x", e, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.size() == 1);
}

TEST_CASE("onedrive: download folgt der Umleitung ohne Authorization") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://graph.test/v1.0/me/drive/items/ABC!12/content",
                  redirect("https://cdn.test/blob?tempauth=x"));
    r.http.expect(HttpMethod::Get, "https://cdn.test/blob", raw(200, "PK.."));
    CloudEntry e;
    e.id = "ABC!12";
    std::vector<u8> zip;
    CHECK(r.drive.download(e, zip, r.cancel) == CloudStatus::Ok);
    CHECK(std::string(zip.begin(), zip.end()) == "PK..");
    CHECK(header(r.http.seen.at(0), "Authorization") == "Bearer old-access");
    CHECK_FALSE(hasHeader(r.http.seen.at(1), "Authorization"));
}

TEST_CASE("onedrive: Umleitung auf http:// → Failed, kein zweiter Abruf") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://graph.test/v1.0/me/drive/items/ABC!12/content",
                  redirect("http://cdn.test/blob"));
    CloudEntry e;
    e.id = "ABC!12";
    std::vector<u8> zip;
    CHECK(r.drive.download(e, zip, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.size() == 1);
}

TEST_CASE("onedrive: neues Paket mit conflictBehavior=fail, danach description per PATCH") {
    Rig r;
    const std::vector<u8> zip = {1, 2, 3, 4};
    r.http.expect(HttpMethod::Put, std::string(kItem) + ":/content?@microsoft.graph.conflictBehavior=fail",
                  jsonResponse(201, kItemJson));
    r.http.expect(HttpMethod::Patch, "https://graph.test/v1.0/me/drive/items/ABC!12", jsonResponse(200, kItemJson));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Ok);
    const HttpRequest& put = r.http.seen.at(0);
    CHECK_FALSE(hasHeader(put, "If-Match"));
    CHECK(put.body.empty());
    CHECK(put.bodyView.data() == zip.data());
    CHECK(header(put, "Content-Type") == "application/zip");
    CHECK(bodyText(r.http.seen.at(1)) ==
          R"({"description":"nl1 hash=)" + std::string(64, 'a') + R"( saved=1700000000 device=Wii%20K%C3%BCche"})");
    CHECK(out.id == "ABC!12");
    CHECK(out.rev == "c:{X},3");
    CHECK(out.hash == std::string(64, 'a'));
    CHECK(out.device == "Wii Küche");
}

TEST_CASE("onedrive: Update schickt If-Match mit der erwarteten Revision") {
    Rig r;
    const std::vector<u8> zip = {1};
    r.http.expect(HttpMethod::Put, std::string(kItem) + ":/content", jsonResponse(200, kItemJson));
    r.http.expect(HttpMethod::Patch, "https://graph.test/v1.0/me/drive/items/ABC!12", jsonResponse(200, kItemJson));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "c:{X},2", out, r.cancel) == CloudStatus::Ok);
    CHECK(header(r.http.seen.at(0), "If-Match") == "\"c:{X},2\"");
    CHECK(r.http.seen.at(0).url.find("conflictBehavior") == std::string::npos);
}

TEST_CASE("onedrive: 412 und 409 beim Upload → RevMismatch, kein PATCH") {
    Rig r;
    const std::vector<u8> zip = {1};
    r.http.expect(HttpMethod::Put, kItem, jsonResponse(412, "{}"));
    r.http.expect(HttpMethod::Put, kItem, jsonResponse(409, "{}"));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "c:{X},2", out, r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.http.seen.size() == 2);
}

TEST_CASE("onedrive: gescheiterter PATCH → Ok, Eintrag ohne Hash") {
    Rig r;
    const std::vector<u8> zip = {1};
    r.http.expect(HttpMethod::Put, kItem, jsonResponse(201, kItemJson));
    r.http.expect(HttpMethod::Patch, "https://graph.test/v1.0/me/drive/items/ABC!12", jsonResponse(500, "{}"));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Ok);
    CHECK(out.rev == "c:{X},3");
    CHECK(out.hash.empty());
}

TEST_CASE("onedrive: über 4 MiB per Upload-Session mit Content-Range und ohne Bearer") {
    Rig r;
    const std::vector<u8> zip(OneDriveProvider::kSimpleMax + 10, 7);
    r.http.expect(HttpMethod::Post, std::string(kItem) + ":/createUploadSession",
                  jsonResponse(200, R"({"uploadUrl":"https://up.test/s/1"})"));
    r.http.expect(HttpMethod::Put, "https://up.test/s/1", jsonResponse(201, kItemJson));
    r.http.expect(HttpMethod::Patch, "https://graph.test/v1.0/me/drive/items/ABC!12", jsonResponse(200, kItemJson));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "c:{X},2", out, r.cancel) == CloudStatus::Ok);
    CHECK(header(r.http.seen.at(0), "If-Match") == "\"c:{X},2\"");
    CHECK(bodyText(r.http.seen.at(0)) == R"({"item":{"@microsoft.graph.conflictBehavior":"replace"}})");
    const HttpRequest& put = r.http.seen.at(1);
    CHECK_FALSE(hasHeader(put, "Authorization"));
    CHECK(header(put, "Content-Range") ==
          "bytes 0-" + std::to_string(zip.size() - 1) + "/" + std::to_string(zip.size()));
    CHECK(put.bodyView.size() == zip.size());
    CHECK(out.rev == "c:{X},3");
}

TEST_CASE("onedrive: Upload-Session ohne Rev fordert conflictBehavior fail; http-uploadUrl → Failed") {
    Rig r;
    const std::vector<u8> zip(OneDriveProvider::kSimpleMax + 1, 7);
    r.http.expect(HttpMethod::Post, std::string(kItem) + ":/createUploadSession",
                  jsonResponse(200, R"({"uploadUrl":"http://up.test/s/1"})"));
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Failed);
    CHECK(bodyText(r.http.seen.at(0)) == R"({"item":{"@microsoft.graph.conflictBehavior":"fail"}})");
    CHECK(r.http.seen.size() == 1);
}

TEST_CASE("onedrive: 401 → ein Refresh über consumers, dann Wiederholung") {
    Rig r;
    r.http.expect(HttpMethod::Get, kItem, jsonResponse(401, "{}"));
    r.http.expect(HttpMethod::Post, "https://login.test/consumers/oauth2/v2.0/token",
                  jsonResponse(200, R"({"access_token":"neu","refresh_token":"r2","expires_in":3600})"));
    r.http.expect(HttpMethod::Get, kItem, jsonResponse(404, "{}"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(header(r.http.seen.at(2), "Authorization") == "Bearer neu");
    CHECK(r.drive.refreshed());
    CHECK(r.drive.tokensChanged());
    CHECK(r.tokens.refresh == "r2");
}
