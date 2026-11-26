#include "core/dropbox_provider.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/json.hpp"
#include "core/url_encode.hpp"

namespace nl {

namespace {

bool idChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// Revisionen sind Kleinbuchstaben-Hex ("a1c10ce0dd78"); nur sie kommen in den Dropbox-API-Arg-Header.
bool safeRev(std::string_view rev) {
    if (rev.empty() || rev.size() > 64) return false;
    return std::all_of(rev.begin(), rev.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// "id:a4ayc_80_OEAAAAAAAAAXw" bzw. "dbid:AAH4f…"
bool safePrefixedId(std::string_view id, std::string_view prefix, std::size_t max) {
    if (id.size() <= prefix.size() || id.size() > max || id.substr(0, prefix.size()) != prefix) return false;
    const std::string_view rest = id.substr(prefix.size());
    return std::all_of(rest.begin(), rest.end(), idChar);
}

std::optional<JsonValue> json(const HttpResponse& r) {
    auto v = parseJson(r.text());
    if (!v.isOk() || v.value().type != JsonType::Object) return std::nullopt;
    return std::move(v.value());
}

HttpRequest rpc(std::string url, std::string body) {
    HttpRequest r;
    r.method = HttpMethod::Post;
    r.url = std::move(url);
    r.headers.emplace_back("Content-Type", "application/json");
    r.body.assign(body.begin(), body.end());
    return r;
}

std::string pathArg(std::string_view path) {
    JsonWriter w;
    w.obj();
    w.key("path");
    w.str(path);
    w.end();
    return w.take();
}

std::string unitPath(std::string_view unit) { return "/" + std::string(unit) + ".zip"; }

bool takeFile(const JsonValue& v, CloudEntry& out) {
    const std::string_view tag = v.string(".tag");
    if (!tag.empty() && tag != "file") return false;  // get_metadata hat .tag, die Upload-Antwort nicht
    CloudEntry e;
    e.id = std::string(v.string("id"));
    e.rev = std::string(v.string("rev"));
    if (!safePrefixedId(e.id, "id:", 128) || !safeRev(e.rev)) return false;
    e.size = static_cast<u64>(std::max<s64>(0, v.integer("size").value_or(0)));
    out = std::move(e);
    return true;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

}  // namespace

OAuthEndpoints dropboxEndpoints(std::string_view base, std::string_view appKey) {
    const std::string b = base.empty() ? std::string("https://api.dropboxapi.com") : std::string(base);
    OAuthEndpoints ep;
    ep.tokenUrl = b + "/oauth2/token";
    ep.clientId = std::string(appKey);
    return ep;
}

std::string dropboxAuthorizeUrl(const OAuthEndpoints& ep, std::string_view challenge, std::string_view base) {
    const std::string b = base.empty() ? std::string("https://www.dropbox.com") : std::string(base);
    return b + "/oauth2/authorize?client_id=" + percentEncode(ep.clientId) +
           "&response_type=code&code_challenge=" + percentEncode(challenge) +
           "&code_challenge_method=S256&token_access_type=offline";
}

DropboxProvider::DropboxProvider(HttpTransport& http, OAuthEndpoints ep, TokenSet& tokens, std::function<u64()> nowMs,
                                 std::string apiBase, std::string contentBase)
    : oauth_(http, std::move(ep), tokens, std::move(nowMs)), api_(std::move(apiBase)), content_(std::move(contentBase)) {}

CloudStatus DropboxProvider::statusOf(const HttpResponse& r) const {
    if (r.status != 409 || oauth_.authFail() != CloudStatus::Ok) return OAuthClient::classify(r, oauth_.authFail());
    const auto j = json(r);
    const std::string_view summary = j ? j->string("error_summary") : std::string_view();
    if (startsWith(summary, "path/not_found")) return CloudStatus::NotFound;
    if (startsWith(summary, "path/conflict")) return CloudStatus::RevMismatch;
    return CloudStatus::Failed;  // z. B. insufficient_space: neu entscheiden hilft nicht
}

CloudStatus DropboxProvider::account(std::string& id, const volatile bool& cancel) {
    const HttpResponse r = oauth_.call(rpc(api_ + "/2/users/get_current_account", "null"), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    const auto j = json(r);
    const std::string_view v = j ? j->string("account_id") : std::string_view();
    if (!safePrefixedId(v, "dbid:", 64)) return CloudStatus::Failed;
    id = std::string(v);
    return CloudStatus::Ok;
}

CloudStatus DropboxProvider::stat(std::string_view unit, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit)) return CloudStatus::Failed;
    const HttpResponse r = oauth_.call(rpc(api_ + "/2/files/get_metadata", pathArg(unitPath(unit))), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s;
    const auto j = json(r);
    CloudEntry e;
    if (!j || !takeFile(*j, e)) return CloudStatus::Failed;
    out = std::move(e);
    return CloudStatus::Ok;
}

CloudStatus DropboxProvider::download(const CloudEntry& entry, std::vector<u8>& zip, const volatile bool& cancel) {
    if (!safeRev(entry.rev)) return CloudStatus::Failed;
    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = content_ + "/2/files/download";
    req.headers.emplace_back("Dropbox-API-Arg", pathArg("rev:" + entry.rev));  // genau die gesehene Revision
    req.maxBody = static_cast<u32>(kMaxCloudPackageBytes);
    HttpResponse r = oauth_.call(std::move(req), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    if (r.body.size() > kMaxCloudPackageBytes) return CloudStatus::Failed;
    zip = std::move(r.body);
    return CloudStatus::Ok;
}

CloudStatus DropboxProvider::upload(std::string_view unit, std::span<const u8> zip, const SaveMeta& meta,
                                    std::string_view expectRev, CloudEntry& out, const volatile bool& cancel) {
    if (!cloudSafeUnitId(unit) || zip.size() > kMaxCloudPackageBytes) return CloudStatus::Failed;
    if (!expectRev.empty() && !safeRev(expectRev)) return CloudStatus::Failed;
    JsonWriter w;
    w.obj();
    w.key("path");
    w.str(unitPath(unit));
    w.key("mode");
    if (expectRev.empty()) {
        w.str("add");  // existiert die Datei schon, meldet Dropbox path/conflict
    } else {
        w.obj();
        w.key(".tag");
        w.str("update");
        w.key("update");
        w.str(expectRev);
        w.end();
    }
    w.key("autorename");
    w.boolean(false);
    w.key("strict_conflict");
    w.boolean(true);
    w.key("mute");
    w.boolean(true);
    w.end();

    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = content_ + "/2/files/upload";
    req.headers.emplace_back("Content-Type", "application/octet-stream");
    req.headers.emplace_back("Dropbox-API-Arg", w.take());
    req.bodyView = zip;
    const HttpResponse r = oauth_.call(std::move(req), cancel);
    const CloudStatus s = statusOf(r);
    if (s != CloudStatus::Ok) return s == CloudStatus::NotFound ? CloudStatus::Failed : s;
    const auto j = json(r);
    CloudEntry e;
    if (!j || !takeFile(*j, e)) return CloudStatus::Failed;
    e.hash = meta.hash;  // unser eigener Inhalt: Hash, Gerät und Zeit sind bekannt
    e.device = meta.device;
    e.saved = meta.saved;
    out = std::move(e);
    return CloudStatus::Ok;
}

RevokeResult DropboxProvider::revoke(const volatile bool& cancel) {
    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = api_ + "/2/auth/token/revoke";
    const HttpResponse r = oauth_.call(std::move(req), cancel);
    return r.status == 200 ? RevokeResult::Ok : RevokeResult::Failed;
}

}  // namespace nl
