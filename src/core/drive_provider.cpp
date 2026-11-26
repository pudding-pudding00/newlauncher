#include "core/drive_provider.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/json.hpp"
#include "core/multipart.hpp"
#include "core/url_encode.hpp"

namespace nl {

namespace {

constexpr std::string_view kFolderName = "newlauncher";
constexpr std::string_view kFolderMime = "application/vnd.google-apps.folder";
constexpr std::string_view kFileFields = "fields=id,version,size,appProperties";
constexpr std::string_view kBoundary = "nl-save-7f3a9c2e5b18d604";
constexpr std::size_t kMaxDeviceBytes = 100;  // appProperties: Schlüssel + Wert höchstens 124 Bytes

// Drive-IDs: [A-Za-z0-9_-]. Alles andere käme nie in eine URL oder Abfrage.
bool safeDriveId(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

std::optional<JsonValue> json(const HttpResponse& r) {
    auto v = parseJson(r.text());
    if (!v.isOk() || v.value().type != JsonType::Object) return std::nullopt;
    return std::move(v.value());
}

std::string_view utf8Prefix(std::string_view s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<u8>(s[n]) & 0xC0) == 0x80) --n;  // nicht mitten in einem Zeichen schneiden
    return s.substr(0, n);
}

void setHeader(HttpHeaders& h, std::string_view name, std::string value) {
    h.erase(std::remove_if(h.begin(), h.end(), [&](const auto& kv) { return kv.first == name; }), h.end());
    h.emplace_back(std::string(name), std::move(value));
}

HttpRequest request(HttpMethod m, std::string url) {
    HttpRequest r;
    r.method = m;
    r.url = std::move(url);
    return r;
}

bool offlineError(NetError e) { return e != NetError::Other && e != NetError::NoMem && e != NetError::None; }

// Datei-Antwort (create/update) → Eintrag; hash/device/saved kommen aus der eigenen Meta.
bool takeFile(const JsonValue& f, CloudEntry& out) {
    out.id = std::string(f.string("id"));
    const auto version = f.integer("version");
    if (!safeDriveId(out.id) || !version) return false;
    out.rev = std::to_string(*version);
    out.size = static_cast<u64>(std::max<s64>(0, f.integer("size").value_or(0)));
    return true;
}

}  // namespace

HttpResponse DriveProvider::Tap::send(const HttpRequest& req, const volatile bool& cancel) {
    HttpResponse r = inner.send(req, cancel);
    lastStatus = r.status;
    lastError = r.error;
    return r;
}

DriveProvider::DriveProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs,
                             std::string apiBase)
    : http_(http), ep_(std::move(ep)), tokens_(tokens), nowMs_(std::move(nowMs)), api_(std::move(apiBase)) {}

CloudStatus DriveProvider::refresh(const volatile bool& cancel) {
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
    if (http_.lastStatus == 0 && offlineError(http_.lastError)) return CloudStatus::Offline;
    return CloudStatus::Failed;
}

HttpResponse DriveProvider::call(HttpRequest req, const volatile bool& cancel) {
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
    setHeader(req.headers, "Authorization", "Bearer " + tokens_.access);
    HttpResponse r = http_.send(req, cancel);
    if (r.status != 401 || refreshedHere) return r;

    // Genau ein Refresh je Anfrage, dann eine Wiederholung.
    const CloudStatus s = refresh(cancel);
    if (s != CloudStatus::Ok) {
        authFail_ = s;
        return HttpResponse{};
    }
    setHeader(req.headers, "Authorization", "Bearer " + tokens_.access);
    return http_.send(req, cancel);
}

namespace {

CloudStatus statusOf(const HttpResponse& r, CloudStatus authFail) {
    if (authFail != CloudStatus::Ok) return authFail;
    if (r.status == 0) return offlineError(r.error) ? CloudStatus::Offline : CloudStatus::Failed;
    if (r.status >= 200 && r.status < 300) return CloudStatus::Ok;
    if (r.status == 401) return CloudStatus::Unauthorized;
    if (r.status == 404) return CloudStatus::NotFound;
    return CloudStatus::Failed;
}

}  // namespace

CloudStatus DriveProvider::account(std::string& id, const volatile bool& cancel) {
    const HttpResponse r = call(request(HttpMethod::Get, api_ + "/drive/v3/about?fields=user(permissionId)"), cancel);
    const CloudStatus s = statusOf(r, authFail_);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    const auto j = json(r);
    const JsonValue* user = j ? j->get("user") : nullptr;
    if (!user || user->string("permissionId").empty()) return CloudStatus::Failed;
    id = std::string(user->string("permissionId"));
    return CloudStatus::Ok;
}

CloudStatus DriveProvider::folder(std::string& id, const volatile bool& cancel) {
    if (!folderId_.empty()) {
        id = folderId_;
        return CloudStatus::Ok;
    }
    std::string q = "name='" + std::string(kFolderName) + "' and mimeType='" + std::string(kFolderMime) +
                    "' and 'root' in parents and trashed=false";
    HttpResponse r =
        call(request(HttpMethod::Get, api_ + "/drive/v3/files?q=" + percentEncode(q) + "&fields=files(id)"), cancel);
    CloudStatus s = statusOf(r, authFail_);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    auto j = json(r);
    const JsonValue* files = j ? j->get("files") : nullptr;
    if (!files || files->type != JsonType::Array) return CloudStatus::Failed;

    std::string found;
    if (!files->items.empty()) {
        found = std::string(files->items.front().string("id"));
    } else {
        JsonWriter w;
        w.obj();
        w.key("name");
        w.str(kFolderName);
        w.key("mimeType");
        w.str(kFolderMime);
        w.end();
        const std::string body = w.take();
        HttpRequest create = request(HttpMethod::Post, api_ + "/drive/v3/files?fields=id");
        create.headers.emplace_back("Content-Type", "application/json; charset=UTF-8");
        create.body.assign(body.begin(), body.end());
        r = call(std::move(create), cancel);
        s = statusOf(r, authFail_);
        if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
        j = json(r);
        if (j) found = std::string(j->string("id"));
    }
    if (!safeDriveId(found)) return CloudStatus::Failed;
    folderId_ = found;
    id = found;
    return CloudStatus::Ok;
}

CloudStatus DriveProvider::stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit)) return CloudStatus::Failed;
    std::string dir;
    if (const CloudStatus s = folder(dir, cancel); s != CloudStatus::Ok) return s;

    const std::string q = "name='" + std::string(unit) + ".zip' and '" + dir + "' in parents and trashed=false";
    const HttpResponse r = call(request(HttpMethod::Get, api_ + "/drive/v3/files?q=" + percentEncode(q) +
                                                             "&fields=files(id,version,size,appProperties)"),
                                cancel);
    const CloudStatus s = statusOf(r, authFail_);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    const auto j = json(r);
    const JsonValue* files = j ? j->get("files") : nullptr;
    if (!files || files->type != JsonType::Array) return CloudStatus::Failed;

    // Doppelte Dateien (zwei Geräte haben gleichzeitig angelegt): die mit der höchsten Version gilt.
    const JsonValue* best = nullptr;
    s64 bestVersion = -1;
    for (const JsonValue& f : files->items) {
        const auto v = f.integer("version");
        if (v && *v > bestVersion) {
            bestVersion = *v;
            best = &f;
        }
    }
    if (!best) return CloudStatus::NotFound;

    CloudEntry e;
    if (!takeFile(*best, e)) return CloudStatus::Failed;
    if (const JsonValue* props = best->get("appProperties")) {
        e.hash = std::string(props->string("hash"));
        e.device = std::string(props->string("device"));
        e.saved = static_cast<u64>(std::max<s64>(0, props->integer("saved").value_or(0)));
    }
    out = std::move(e);
    return CloudStatus::Ok;
}

CloudStatus DriveProvider::download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) {
    if (!safeDriveId(entry.id)) return CloudStatus::Failed;
    HttpRequest req = request(HttpMethod::Get, api_ + "/drive/v3/files/" + entry.id + "?alt=media");
    req.maxBody = static_cast<u32>(kMaxCloudPackageBytes);
    HttpResponse r = call(std::move(req), cancel);
    const CloudStatus s = statusOf(r, authFail_);
    if (s != CloudStatus::Ok) return s;
    if (r.body.size() > kMaxCloudPackageBytes) return CloudStatus::Failed;
    zip = std::move(r.body);
    return CloudStatus::Ok;
}

CloudStatus DriveProvider::upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                                  std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit)) return CloudStatus::Failed;
    std::string dir;
    if (const CloudStatus s = folder(dir, cancel); s != CloudStatus::Ok) return s;

    // Drive kennt kein If-Match: die Revision wird direkt vorher geprüft (kleines Zeitfenster, siehe Spec).
    CloudEntry current;
    const CloudStatus st = stat(unit, current, cancel);
    if (st != CloudStatus::Ok && st != CloudStatus::NotFound) return st;
    const bool exists = st == CloudStatus::Ok;
    if (exists ? current.rev != expectRev : !expectRev.empty()) return CloudStatus::RevMismatch;

    JsonWriter w;
    w.obj();
    w.key("name");
    w.str(std::string(unit) + ".zip");
    w.key("mimeType");
    w.str("application/zip");
    if (!exists) {
        w.key("parents");
        w.arr();
        w.str(dir);
        w.end();
    }
    w.key("appProperties");
    w.obj();
    w.key("hash");
    w.str(meta.hash);
    w.key("saved");
    w.str(std::to_string(meta.saved));
    w.key("device");
    w.str(utf8Prefix(meta.device, kMaxDeviceBytes));
    w.key("unit");
    w.str(unit);
    w.end();
    w.end();
    const std::string metaJson = w.take();

    const HttpMethod method = exists ? HttpMethod::Patch : HttpMethod::Post;
    const std::string base = api_ + "/upload/drive/v3/files" + (exists ? "/" + current.id : std::string());
    const std::string marker = "--" + std::string(kBoundary);
    const bool boundaryInData =
        std::search(zip.begin(), zip.end(), marker.begin(), marker.end()) != zip.end();

    HttpResponse r;
    if (zip.size() <= kMultipartMax && !boundaryInData) {
        HttpRequest req = request(method, base + "?uploadType=multipart&" + std::string(kFileFields));
        req.headers.emplace_back("Content-Type", multipartContentType(kBoundary));
        req.body = multipartRelated(kBoundary, metaJson, zip);
        r = call(std::move(req), cancel);
    } else {
        HttpRequest start = request(method, base + "?uploadType=resumable&" + std::string(kFileFields));
        start.headers.emplace_back("Content-Type", "application/json; charset=UTF-8");
        start.headers.emplace_back("X-Upload-Content-Type", "application/zip");
        start.headers.emplace_back("X-Upload-Content-Length", std::to_string(zip.size()));
        start.body.assign(metaJson.begin(), metaJson.end());
        const HttpResponse session = call(std::move(start), cancel);
        if (const CloudStatus s = statusOf(session, authFail_); s != CloudStatus::Ok) {
            return s == CloudStatus::NotFound ? CloudStatus::RevMismatch : s;
        }
        // Die Sitzungs-URL bekommt das Bearer-Token: nur auf dem eigenen API-Ursprung.
        const std::string location(session.header("Location"));
        if (location.rfind(api_ + "/", 0) != 0) return CloudStatus::Failed;
        HttpRequest put = request(HttpMethod::Put, location);
        put.headers.emplace_back("Content-Type", "application/zip");
        put.body.assign(zip.begin(), zip.end());
        r = call(std::move(put), cancel);
    }
    const CloudStatus s = statusOf(r, authFail_);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::RevMismatch : s;
    const auto j = json(r);
    CloudEntry e;
    if (!j || !takeFile(*j, e)) return CloudStatus::Failed;
    e.hash = meta.hash;
    e.device = std::string(utf8Prefix(meta.device, kMaxDeviceBytes));
    e.saved = meta.saved;
    out = std::move(e);
    return CloudStatus::Ok;
}

}  // namespace nl
