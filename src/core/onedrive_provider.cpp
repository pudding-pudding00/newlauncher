#include "core/onedrive_provider.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

#include "core/json.hpp"
#include "core/url_encode.hpp"

namespace nl {

namespace {

constexpr std::size_t kMaxDeviceBytes = 100;

// Graph-IDs (Privatkonten z. B. "D4648F06C91D9D3D!54927"): nur diese Zeichen kommen in eine URL.
bool safeGraphId(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '!' || c == '.' ||
               c == '_' || c == '-';
    });
}

std::optional<JsonValue> json(const HttpResponse& r) {
    auto v = parseJson(r.text());
    if (!v.isOk() || v.value().type != JsonType::Object) return std::nullopt;
    return std::move(v.value());
}

HttpRequest request(HttpMethod m, std::string url) {
    HttpRequest r;
    r.method = m;
    r.url = std::move(url);
    return r;
}

// cTag kommt in Anführungszeichen ("\"c:{…},3\""); gespeichert wird er ohne, für If-Match wieder mit.
std::string unquote(std::string_view tag) {
    if (tag.size() >= 2 && tag.front() == '"' && tag.back() == '"') tag = tag.substr(1, tag.size() - 2);
    return std::string(tag);
}

bool takeItem(const JsonValue& v, CloudEntry& out) {
    out.id = std::string(v.string("id"));
    out.rev = unquote(v.string("cTag"));
    if (!safeGraphId(out.id) || out.rev.empty()) return false;
    out.size = static_cast<u64>(std::max<s64>(0, v.integer("size").value_or(0)));
    return true;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::string> percentDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') {
            out.push_back(s[i]);
            continue;
        }
        if (i + 2 >= s.size()) return std::nullopt;
        const int hi = hexValue(s[i + 1]), lo = hexValue(s[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return out;
}

std::string_view utf8Prefix(std::string_view s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<u8>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

void setMetaFields(const SaveMeta& meta, CloudEntry& out) {
    out.hash = meta.hash;
    out.device = std::string(utf8Prefix(meta.device, kMaxDeviceBytes));
    out.saved = meta.saved;
}

}  // namespace

OAuthEndpoints microsoftEndpoints(std::string_view base, std::string_view clientId) {
    const std::string b = base.empty() ? std::string("https://login.microsoftonline.com") : std::string(base);
    OAuthEndpoints ep;
    ep.deviceUrl = b + "/consumers/oauth2/v2.0/devicecode";
    ep.tokenUrl = b + "/consumers/oauth2/v2.0/token";
    ep.clientId = std::string(clientId);
    ep.scope = std::string(kOneDriveScope);
    return ep;
}

std::string oneDriveDescription(const SaveMeta& meta) {
    return "nl1 hash=" + meta.hash + " saved=" + std::to_string(meta.saved) +
           " device=" + percentEncode(utf8Prefix(meta.device, kMaxDeviceBytes));
}

bool parseOneDriveDescription(std::string_view text, CloudEntry& out) {
    if (text.substr(0, 4) != "nl1 ") return false;
    std::string hash, device;
    u64 saved = 0;
    std::size_t pos = 4;
    while (pos < text.size()) {
        std::size_t end = text.find(' ', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view part = text.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t eq = part.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view k = part.substr(0, eq), v = part.substr(eq + 1);
        if (k == "hash") {
            hash = std::string(v);
        } else if (k == "saved") {
            std::from_chars(v.data(), v.data() + v.size(), saved);
        } else if (k == "device") {
            auto d = percentDecode(v);
            if (d) device = std::move(*d);
        }
    }
    if (hash.empty() || !std::all_of(hash.begin(), hash.end(), [](char c) { return hexValue(c) >= 0; })) return false;
    out.hash = std::move(hash);
    out.device = std::move(device);
    out.saved = saved;
    return true;
}

OneDriveProvider::OneDriveProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens,
                                   std::function<u64()> nowMs, std::string apiBase)
    : oauth_(http, std::move(ep), tokens, std::move(nowMs)), api_(std::move(apiBase)) {}

std::string OneDriveProvider::itemPath(std::string_view unit) const {
    return api_ + "/v1.0/me/drive/special/approot:/" + std::string(unit) + ".zip";
}

CloudStatus OneDriveProvider::account(std::string& id, const volatile bool& cancel) {
    const HttpResponse r = oauth_.call(request(HttpMethod::Get, api_ + "/v1.0/me/drive?$select=id"), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    const auto j = json(r);
    const std::string_view v = j ? j->string("id") : std::string_view();
    if (v.size() > 64 || !safeGraphId(v)) return CloudStatus::Failed;
    id = std::string(v);
    return CloudStatus::Ok;
}

CloudStatus OneDriveProvider::stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit)) return CloudStatus::Failed;
    const HttpResponse r =
        oauth_.call(request(HttpMethod::Get, itemPath(unit) + "?$select=id,cTag,size,description"), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s;
    const auto j = json(r);
    CloudEntry e;
    if (!j || !takeItem(*j, e)) return CloudStatus::Failed;
    parseOneDriveDescription(j->string("description"), e);  // ohne Meta bleibt hash leer (cloud_plan lädt dann)
    out = std::move(e);
    return CloudStatus::Ok;
}

CloudStatus OneDriveProvider::download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) {
    if (!safeGraphId(entry.id)) return CloudStatus::Failed;
    HttpRequest req = request(HttpMethod::Get, api_ + "/v1.0/me/drive/items/" + entry.id + "/content");
    req.maxBody = static_cast<u32>(kMaxCloudPackageBytes);
    HttpResponse r = oauth_.call(std::move(req), cancel);
    if (r.status == 302 || r.status == 301 || r.status == 303 || r.status == 307) {
        const std::string location(r.header("Location"));
        if (location.rfind("https://", 0) != 0) return CloudStatus::Failed;  // nie unverschlüsselt, nie mit Token
        HttpRequest dl = request(HttpMethod::Get, location);
        dl.maxBody = static_cast<u32>(kMaxCloudPackageBytes);
        r = oauth_.plain(std::move(dl), cancel);
        if (r.status >= 300 && r.status < 400) return CloudStatus::Failed;
    }
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    if (r.body.size() > kMaxCloudPackageBytes) return CloudStatus::Failed;
    zip = std::move(r.body);
    return CloudStatus::Ok;
}

CloudStatus OneDriveProvider::upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                                     std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit) || zip.size() > kMaxCloudPackageBytes) return CloudStatus::Failed;
    const std::string ifMatch = "\"" + std::string(expectRev) + "\"";

    HttpResponse r;
    if (zip.size() <= kSimpleMax) {
        std::string url = itemPath(unit) + ":/content";
        if (expectRev.empty()) url += "?@microsoft.graph.conflictBehavior=fail";
        HttpRequest put = request(HttpMethod::Put, std::move(url));
        put.headers.emplace_back("Content-Type", "application/zip");
        if (!expectRev.empty()) put.headers.emplace_back("If-Match", ifMatch);
        put.bodyView = zip;  // geliehen; zip lebt bis nach call()
        r = oauth_.call(std::move(put), cancel);
    } else {
        HttpRequest start = request(HttpMethod::Post, itemPath(unit) + ":/createUploadSession");
        start.headers.emplace_back("Content-Type", "application/json");
        if (!expectRev.empty()) start.headers.emplace_back("If-Match", ifMatch);
        const std::string body = std::string(R"({"item":{"@microsoft.graph.conflictBehavior":")") +
                                 (expectRev.empty() ? "fail" : "replace") + "\"}}";
        start.body.assign(body.begin(), body.end());
        const HttpResponse session = oauth_.call(std::move(start), cancel);
        if (const CloudStatus s = statusOf(session); s != CloudStatus::Ok) {
            return s == CloudStatus::NotFound ? CloudStatus::RevMismatch : s;
        }
        const auto j = json(session);
        const std::string uploadUrl(j ? j->string("uploadUrl") : std::string_view());
        if (uploadUrl.rfind("https://", 0) != 0) return CloudStatus::Failed;
        // Die Upload-URL ist vorab signiert und liegt oft auf einem anderen Host: kein Bearer-Token mitschicken.
        HttpRequest put = request(HttpMethod::Put, uploadUrl);
        put.headers.emplace_back("Content-Range", "bytes 0-" + std::to_string(zip.size() - 1) + "/" +
                                                      std::to_string(zip.size()));
        put.bodyView = zip;
        r = oauth_.plain(std::move(put), cancel);
    }
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::RevMismatch : s;
    const auto item = json(r);
    CloudEntry e;
    if (!item || !takeItem(*item, e)) return CloudStatus::Failed;

    // Meta nachtragen. Scheitert nur das, ist der Inhalt trotzdem oben: Ok mit leerem Hash, der nächste Abgleich
    // bewertet das Paket selbst.
    HttpRequest patch = request(HttpMethod::Patch, api_ + "/v1.0/me/drive/items/" + e.id);
    patch.headers.emplace_back("Content-Type", "application/json");
    JsonWriter w;
    w.obj();
    w.key("description");
    w.str(oneDriveDescription(meta));
    w.end();
    const std::string pbody = w.take();
    patch.body.assign(pbody.begin(), pbody.end());
    const HttpResponse pr = oauth_.call(std::move(patch), cancel);
    if (statusOf(pr) == CloudStatus::Ok) setMetaFields(meta, e);
    out = std::move(e);
    return CloudStatus::Ok;
}

}  // namespace nl
