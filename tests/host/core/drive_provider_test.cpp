#include "core/drive_provider.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "support/fake_transport.hpp"

using namespace nl;
using nl::test::FakeTransport;
using nl::test::jsonResponse;
using nl::test::offlineResponse;

namespace {

constexpr const char* kApi = "https://drive.test";
constexpr const char* kFolderQuery = "https://drive.test/drive/v3/files?q=";
constexpr const char* kFolderJson = R"({"files":[{"id":"F1"}]})";

struct Rig {
    FakeTransport http;
    TokenSet tokens{"old-access", "refresh-1", 10'000'000};
    u64 now = 1'000;
    DriveProvider drive{http, googleEndpoints("https://oauth.test", "cid", "sec"), tokens, [this] { return now; },
                        kApi};
    volatile bool cancel = false;
};

std::string header(const HttpRequest& r, std::string_view name) {
    for (const auto& [k, v] : r.headers) {
        if (k == name) return v;
    }
    return {};
}

std::string bodyText(const HttpRequest& r) { return {r.body.begin(), r.body.end()}; }

SaveMeta meta() {
    SaveMeta m;
    m.unit = "wii-RMCP";
    m.title = "Mario Kart";
    m.device = "Wohnzimmer";
    m.saved = 1'700'000'000;
    m.hash = std::string(64, 'a');
    return m;
}

const char* kStatJson =
    R"({"files":[{"id":"Z1","version":"7","size":"1234","appProperties":{"hash":"abc","saved":"1700000000","device":"Wohnzimmer"}}]})";

}  // namespace

TEST_CASE("cloudSafeUnitId") {
    CHECK(cloudSafeUnitId("wii-RMCP"));
    CHECK(cloudSafeUnitId("ch-00010001484a4145"));
    CHECK(cloudSafeUnitId("gba-zelda.minish-1a2b3c4d"));
    CHECK_FALSE(cloudSafeUnitId(""));
    CHECK_FALSE(cloudSafeUnitId(".hidden"));
    CHECK_FALSE(cloudSafeUnitId("a'b"));
    CHECK_FALSE(cloudSafeUnitId("a b"));
    CHECK_FALSE(cloudSafeUnitId("a/b"));
    CHECK_FALSE(cloudSafeUnitId(std::string(65, 'a')));
}

TEST_CASE("drive: account liest permissionId mit Bearer-Header") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about?fields=user(permissionId)",
                  jsonResponse(200, R"({"user":{"permissionId":"0815"}})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Ok);
    CHECK(id == "0815");
    CHECK(header(r.http.seen.at(0), "Authorization") == "Bearer old-access");
}

TEST_CASE("drive: fehlender Ordner wird angelegt, danach stat NotFound") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    r.http.expect(HttpMethod::Post, "https://drive.test/drive/v3/files?fields=id", jsonResponse(200, R"({"id":"F1"})"));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(r.http.done());
    // Ordnersuche: Name, Typ, nicht im Papierkorb; Anlegen mit Ordner-MIME-Typ
    CHECK(r.http.seen[0].url.find("newlauncher") != std::string::npos);
    CHECK(r.http.seen[0].url.find("trashed%3Dfalse") != std::string::npos);
    CHECK(bodyText(r.http.seen[1]).find("application/vnd.google-apps.folder") != std::string::npos);
    // stat sucht im Ordner F1 nach wii-RMCP.zip
    CHECK(r.http.seen[2].url.find("wii-RMCP.zip") != std::string::npos);
    CHECK(r.http.seen[2].url.find("F1") != std::string::npos);
}

TEST_CASE("drive: Ordner-ID wird zwischengespeichert") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    CloudEntry e;
    CHECK(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(r.drive.stat("wii-SMNP", e, r.cancel) == CloudStatus::NotFound);
    CHECK(r.http.done());
}

TEST_CASE("drive: stat liest version, Größe und appProperties; rev = id:version") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kStatJson));
    CloudEntry e;
    REQUIRE(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.id == "Z1");
    CHECK(e.rev == "Z1:7");
    CHECK(e.size == 1234);
    CHECK(e.hash == "abc");
    CHECK(e.saved == 1'700'000'000);
    CHECK(e.device == "Wohnzimmer");
}

TEST_CASE("drive: doppelte Dateien → auf jedem Gerät die zuerst angelegte, rev unterscheidet sie") {
    // Zwei Geräte haben gleichzeitig angelegt: beide Dateien haben version 1. Versionen zählen je Datei und sagen
    // zwischen zwei Dateien nichts; alle Geräte müssen dieselbe wählen (orderBy=createdTime, die erste).
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery,
                  jsonResponse(200, R"({"files":[{"id":"ZA","version":"1","size":"9"},)"
                                    R"({"id":"ZB","version":"4","size":"9"}]})"));
    CloudEntry e;
    REQUIRE(r.drive.stat("wii-RMCP", e, r.cancel) == CloudStatus::Ok);
    CHECK(e.id == "ZA");
    CHECK(e.rev == "ZA:1");
    CHECK(r.http.seen.at(0).url.find("&orderBy=createdTime") != std::string::npos);  // Ordner ebenso
    CHECK(r.http.seen.at(1).url.find("&orderBy=createdTime") != std::string::npos);
}

TEST_CASE("drive: Upload auf eine andere Datei mit gleicher Version → RevMismatch") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kStatJson));  // Z1, version 7
    const std::vector<u8> zip = {1};
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "Z0:7", out, r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.http.seen.size() == 2);
}

TEST_CASE("drive: unsichere Einheiten-ID wird nie gesendet") {
    Rig r;
    CloudEntry e;
    CHECK(r.drive.stat("x' or name contains '", e, r.cancel) == CloudStatus::Failed);
    CHECK(r.http.seen.empty());
}

TEST_CASE("drive: neue Datei = multipart POST mit parents und appProperties") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    r.http.expect(HttpMethod::Post, "https://drive.test/upload/drive/v3/files?uploadType=multipart",
                  jsonResponse(200, R"({"id":"Z9","version":"1","size":"3"})"));
    const std::vector<u8> zip = {1, 2, 3};
    CloudEntry out;
    REQUIRE(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Ok);
    CHECK(out.id == "Z9");
    CHECK(out.rev == "Z9:1");
    CHECK(out.hash == meta().hash);
    const HttpRequest& up = r.http.seen.at(2);
    CHECK(header(up, "Authorization") == "Bearer old-access");
    CHECK(header(up, "Content-Type").rfind("multipart/related; boundary=", 0) == 0);
    const std::string body = bodyText(up);
    CHECK(body.find(R"("parents":["F1"])") != std::string::npos);
    CHECK(body.find(R"("name":"wii-RMCP.zip")") != std::string::npos);
    CHECK(body.find(R"("hash":")" + meta().hash + "\"") != std::string::npos);
    CHECK(body.find(R"("saved":"1700000000")") != std::string::npos);
    CHECK(body.find(R"("device":"Wohnzimmer")") != std::string::npos);
    CHECK(body.find(R"("unit":"wii-RMCP")") != std::string::npos);
}

TEST_CASE("drive: vorhandene Datei mit passender Revision = PATCH ohne parents") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kStatJson));
    r.http.expect(HttpMethod::Patch, "https://drive.test/upload/drive/v3/files/Z1?uploadType=multipart",
                  jsonResponse(200, R"({"id":"Z1","version":"8","size":"3"})"));
    const std::vector<u8> zip = {1, 2, 3};
    CloudEntry out;
    REQUIRE(r.drive.upload("wii-RMCP", zip, meta(), "Z1:7", out, r.cancel) == CloudStatus::Ok);
    CHECK(out.rev == "Z1:8");
    CHECK(bodyText(r.http.seen.at(2)).find("parents") == std::string::npos);
}

TEST_CASE("drive: abweichende Revision → RevMismatch ohne Upload") {
    for (const char* expect : {"Z1:6", "7", ""}) {
        Rig r;
        r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
        r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kStatJson));
        const std::vector<u8> zip = {1};
        CloudEntry out;
        CHECK(r.drive.upload("wii-RMCP", zip, meta(), expect, out, r.cancel) == CloudStatus::RevMismatch);
        CHECK(r.http.seen.size() == 2);
    }
    // erwartet vorhanden, aber in der Cloud gelöscht
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    const std::vector<u8> zip = {1};
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "Z1:7", out, r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.http.seen.size() == 2);
}

TEST_CASE("drive: großes Paket → resumable POST, dann ein PUT an Location") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    HttpResponse session = jsonResponse(200, "");
    session.headers.emplace_back("Location", "https://drive.test/upload/session/abc");
    r.http.expect(HttpMethod::Post, "https://drive.test/upload/drive/v3/files?uploadType=resumable", session);
    r.http.expect(HttpMethod::Put, "https://drive.test/upload/session/abc",
                  jsonResponse(200, R"({"id":"Z9","version":"1","size":"6291456"})"));
    const std::vector<u8> zip(6 * 1024 * 1024, 0x5a);
    CloudEntry out;
    REQUIRE(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Ok);
    CHECK(r.http.done());
    const HttpRequest& start = r.http.seen.at(2);
    CHECK(header(start, "X-Upload-Content-Length") == "6291456");
    CHECK(header(start, "X-Upload-Content-Type") == "application/zip");
    CHECK(bodyText(start).find(R"("parents":["F1"])") != std::string::npos);
    const HttpRequest& put = r.http.seen.at(3);
    CHECK(put.body.size() == zip.size());
    CHECK(header(put, "Content-Type") == "application/zip");
}

TEST_CASE("drive: resumable ohne Location → Failed") {
    Rig r;
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, kFolderJson));
    r.http.expect(HttpMethod::Get, kFolderQuery, jsonResponse(200, R"({"files":[]})"));
    r.http.expect(HttpMethod::Post, "https://drive.test/upload/drive/v3/files?uploadType=resumable",
                  jsonResponse(200, ""));
    const std::vector<u8> zip(6 * 1024 * 1024, 0);
    CloudEntry out;
    CHECK(r.drive.upload("wii-RMCP", zip, meta(), "", out, r.cancel) == CloudStatus::Failed);
}

TEST_CASE("drive: 401 → genau ein Refresh, Wiederholung mit neuem Token") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", jsonResponse(401, R"({"error":{}})"));
    r.http.expect(HttpMethod::Post, "https://oauth.test/token",
                  jsonResponse(200, R"({"access_token":"new-access","expires_in":3599})"));
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about",
                  jsonResponse(200, R"({"user":{"permissionId":"0815"}})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Ok);
    CHECK(r.http.done());
    CHECK(header(r.http.seen.at(2), "Authorization") == "Bearer new-access");
    CHECK(r.drive.tokensChanged());
    CHECK(r.drive.refreshed());
    CHECK(r.tokens.access == "new-access");
    CHECK(r.tokens.refresh == "refresh-1");
}

TEST_CASE("drive: zweites 401 nach Refresh → Unauthorized, kein zweiter Refresh") {
    Rig r;
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", jsonResponse(401, "{}"));
    r.http.expect(HttpMethod::Post, "https://oauth.test/token",
                  jsonResponse(200, R"({"access_token":"new-access","expires_in":3599})"));
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", jsonResponse(401, "{}"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Unauthorized);
    CHECK(r.http.done());
    CHECK(r.http.seen.size() == 3);
}

TEST_CASE("drive: abgelaufenes Token wird vorher erneuert; invalid_grant → Unauthorized") {
    Rig r;
    r.tokens.expiresAtMs = 0;
    r.http.expect(HttpMethod::Post, "https://oauth.test/token", jsonResponse(400, R"({"error":"invalid_grant"})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Unauthorized);
    CHECK(r.http.done());
    CHECK_FALSE(r.drive.tokensChanged());
}

TEST_CASE("drive: abgelaufenes Token wird vorher erneuert, dann Anfrage") {
    Rig r;
    r.tokens.expiresAtMs = 0;
    r.http.expect(HttpMethod::Post, "https://oauth.test/token",
                  jsonResponse(200, R"({"access_token":"fresh","expires_in":3599})"));
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about",
                  jsonResponse(200, R"({"user":{"permissionId":"0815"}})"));
    std::string id;
    CHECK(r.drive.account(id, r.cancel) == CloudStatus::Ok);
    CHECK(header(r.http.seen.at(1), "Authorization") == "Bearer fresh");
}

TEST_CASE("drive: offline → Offline, Serverfehler → Failed") {
    {
        // nach dem Laden ist das Token immer abgelaufen: offline beim Refresh ist trotzdem Offline
        Rig r;
        r.tokens.expiresAtMs = 0;
        r.http.expect(HttpMethod::Post, "https://oauth.test/token", offlineResponse());
        std::string id;
        CHECK(r.drive.account(id, r.cancel) == CloudStatus::Offline);
    }
    {
        Rig r;
        r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", offlineResponse());
        std::string id;
        CHECK(r.drive.account(id, r.cancel) == CloudStatus::Offline);
    }
    {
        Rig r;
        r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", jsonResponse(500, "{}"));
        std::string id;
        CHECK(r.drive.account(id, r.cancel) == CloudStatus::Failed);
    }
    {
        Rig r;
        r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/about", jsonResponse(200, "kein json"));
        std::string id;
        CHECK(r.drive.account(id, r.cancel) == CloudStatus::Failed);
    }
}

TEST_CASE("drive: download mit alt=media und Paketgrenze") {
    Rig r;
    HttpResponse media;
    media.status = 200;
    media.error = NetError::None;
    media.body = {9, 8, 7};
    r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/files/Z1?alt=media", media);
    CloudEntry e;
    e.id = "Z1";
    std::vector<u8> zip;
    REQUIRE(r.drive.download(e, zip, r.cancel) == CloudStatus::Ok);
    CHECK(zip == std::vector<u8>{9, 8, 7});
    CHECK(r.http.seen.at(0).maxBody == kMaxCloudPackageBytes);
}

TEST_CASE("drive: download zu groß → Failed, 404 → NotFound") {
    {
        Rig r;
        HttpResponse big;
        big.status = 0;
        big.error = NetError::Other;  // Body über maxBody
        r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/files/Z1?alt=media", big);
        CloudEntry e;
        e.id = "Z1";
        std::vector<u8> zip;
        CHECK(r.drive.download(e, zip, r.cancel) == CloudStatus::Failed);
    }
    {
        Rig r;
        r.http.expect(HttpMethod::Get, "https://drive.test/drive/v3/files/Z1?alt=media", jsonResponse(404, "{}"));
        CloudEntry e;
        e.id = "Z1";
        std::vector<u8> zip;
        CHECK(r.drive.download(e, zip, r.cancel) == CloudStatus::NotFound);
    }
}

TEST_CASE("cloudStatusName") {
    CHECK(std::string(cloudStatusName(CloudStatus::Ok)) == "ok");
    CHECK(std::string(cloudStatusName(CloudStatus::RevMismatch)) == "revmismatch");
    CHECK(std::string(cloudStatusName(CloudStatus::Offline)) == "offline");
}
