#include "core/pairing_http.hpp"

#include <utility>

namespace nl {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// Vergleich ohne frühen Ausstieg: Laufzeit hängt nur von der Länge der Eingabe ab, nicht davon, wie viele Zeichen
// des Tokens schon stimmen.
bool tokenMatches(std::string_view got, std::string_view want) {
    unsigned diff = got.size() == want.size() ? 0u : 1u;
    for (std::size_t i = 0; i < got.size(); ++i)
        diff |= static_cast<unsigned>(static_cast<u8>(got[i]) ^ static_cast<u8>(want.empty() ? 0 : want[i % want.size()]));
    return diff == 0 && !want.empty();
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::string> formDecode(std::string_view v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '+') {
            out.push_back(' ');
        } else if (v[i] == '%') {
            if (i + 2 >= v.size()) return std::nullopt;
            const int hi = hexVal(v[i + 1]), lo = hexVal(v[i + 2]);
            if (hi < 0 || lo < 0) return std::nullopt;
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else {
            out.push_back(v[i]);
        }
    }
    return out;
}

bool codeChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

std::optional<std::string> extractCode(std::string_view body) {
    while (!body.empty()) {
        const std::size_t amp = body.find('&');
        const std::string_view pair = body.substr(0, amp);
        body = amp == std::string_view::npos ? std::string_view() : body.substr(amp + 1);
        if (pair.substr(0, 5) != "code=") continue;
        const auto decoded = formDecode(pair.substr(5));
        if (!decoded) return std::nullopt;
        const std::string_view code = trim(*decoded);
        if (code.empty() || code.size() > kPairMaxCode) return std::nullopt;
        for (char c : code)
            if (!codeChar(c)) return std::nullopt;
        return std::string(code);
    }
    return std::nullopt;
}

std::string htmlEscape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        default: return "Error";
    }
}

std::string shell(std::string_view title, std::string_view inner) {
    return "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>" +
           std::string(title) +
           "</title><style>body{font-family:sans-serif;max-width:32em;margin:2em auto;padding:0 1em}"
           "a.b,button{display:block;padding:.8em;margin:1em 0;font-size:1.1em;text-align:center;"
           "background:#0061fe;color:#fff;border:0;border-radius:.4em;text-decoration:none;width:100%}"
           "input{font-size:1.2em;width:100%;padding:.5em;box-sizing:border-box}</style></head><body>" +
           std::string(inner) + "</body></html>";
}

PairingReply plain(int status, std::string_view text, std::string marker) {
    PairingReply r;
    r.status = status;
    r.body = shell("newlauncher", "<p>" + std::string(text) + "</p>");
    r.log = !marker.empty();
    r.marker = std::move(marker);
    return r;
}

}  // namespace

std::optional<HttpRequestHead> parseHttpHead(std::string_view raw) {
    const std::size_t end = raw.find("\r\n\r\n");
    if (end == std::string_view::npos) return std::nullopt;
    HttpRequestHead h;
    h.headerBytes = end + 4;
    std::string_view head = raw.substr(0, end);
    const std::size_t eol = head.find("\r\n");
    const std::string_view line = head.substr(0, eol);
    head = eol == std::string_view::npos ? std::string_view() : head.substr(eol + 2);
    const std::size_t s1 = line.find(' ');
    if (s1 == std::string_view::npos || s1 == 0) return std::nullopt;
    const std::size_t s2 = line.find(' ', s1 + 1);
    if (s2 == std::string_view::npos || s2 == s1 + 1) return std::nullopt;
    if (line.substr(s2 + 1).substr(0, 5) != "HTTP/") return std::nullopt;
    h.method = std::string(line.substr(0, s1));
    h.path = std::string(line.substr(s1 + 1, s2 - s1 - 1));
    if (h.path.front() != '/') return std::nullopt;
    while (!head.empty()) {
        const std::size_t e = head.find("\r\n");
        const std::string_view hl = head.substr(0, e);
        head = e == std::string_view::npos ? std::string_view() : head.substr(e + 2);
        const std::size_t colon = hl.find(':');
        if (colon == std::string_view::npos) continue;
        if (!equalsNoCase(trim(hl.substr(0, colon)), "content-length")) continue;
        const std::string_view v = trim(hl.substr(colon + 1));
        if (v.empty() || v.size() > 9) return std::nullopt;  // weit über kPairMaxRequest, kein Überlauf
        std::size_t n = 0;
        for (char c : v) {
            if (c < '0' || c > '9') return std::nullopt;
            n = n * 10 + static_cast<std::size_t>(c - '0');
        }
        h.contentLength = n;
    }
    return h;
}

std::string buildPairingResponse(const PairingReply& reply) {
    std::string r = "HTTP/1.1 " + std::to_string(reply.status) + " " + reason(reply.status) + "\r\n";
    r += "Content-Type: text/html; charset=utf-8\r\n";
    r += "Content-Length: " + std::to_string(reply.body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\n";
    r += "Referrer-Policy: no-referrer\r\n";  // das Token im Pfad nicht an Dropbox weitergeben
    r += "Connection: close\r\n\r\n";
    r += reply.body;
    return r;
}

PairingSession::PairingSession(std::string token, std::string authorizeUrl, u64 nowMs, bool english)
    : token_(std::move(token)), authorizeUrl_(std::move(authorizeUrl)), startMs_(nowMs), english_(english) {}

bool PairingSession::expired(u64 nowMs) const { return nowMs >= startMs_ && nowMs - startMs_ >= kPairTimeoutMs; }

PairingReply PairingSession::page(int status, std::string_view message) const {
    const bool en = english_;
    std::string inner;
    inner += en ? "<h1>Connect Dropbox</h1>" : "<h1>Dropbox verbinden</h1>";
    if (!message.empty()) inner += "<p><b>" + std::string(message) + "</b></p>";
    inner += en ? "<p>1. Allow newlauncher on Dropbox. Dropbox then shows a code.</p>"
                : "<p>1. newlauncher bei Dropbox zulassen. Dropbox zeigt danach einen Code an.</p>";
    inner += "<a class=\"b\" href=\"" + htmlEscape(authorizeUrl_) + "\" target=\"_blank\" rel=\"noreferrer\">" +
             (en ? "Allow on Dropbox" : "Bei Dropbox zulassen") + "</a>";
    inner += en ? "<p>2. Copy the code and send it to the Wii:</p>"
                : "<p>2. Den Code kopieren und an die Wii senden:</p>";
    inner += "<form method=\"post\" action=\"/" + htmlEscape(token_) +
             "\"><input name=\"code\" autocomplete=\"off\" autocapitalize=\"off\" spellcheck=\"false\" maxlength=\"128\">"
             "<button type=\"submit\">" +
             std::string(en ? "Send code" : "Code senden") + "</button></form>";
    PairingReply r;
    r.status = status;
    r.body = shell(en ? "Connect Dropbox" : "Dropbox verbinden", inner);
    return r;
}

PairingReply PairingSession::handle(std::string_view request, u64 nowMs) {
    const bool en = english_;
    if (request.size() > kPairMaxRequest) return plain(413, en ? "Request too large." : "Anfrage zu groß.", "reject");
    const auto head = parseHttpHead(request);
    if (!head) return plain(400, en ? "Bad request." : "Ungültige Anfrage.", "");
    if (head->headerBytes + head->contentLength > kPairMaxRequest)
        return plain(413, en ? "Request too large." : "Anfrage zu groß.", "reject");
    if (!tokenMatches(std::string_view(head->path).substr(1), token_))
        return plain(404, en ? "Not found." : "Nicht gefunden.", "reject");
    const std::string_view gone =
        en ? "This page is no longer active. Open it again from the Wii." : "Diese Seite ist nicht mehr aktiv. Auf der Wii neu öffnen.";
    if (expired(nowMs)) return plain(410, gone, "reject");
    if (head->method == "GET") {
        if (consumed_) return plain(410, gone, "");
        return page(200, "");
    }
    if (head->method != "POST") return plain(405, en ? "Method not allowed." : "Methode nicht erlaubt.", "");
    if (consumed_) return plain(410, gone, "reject");
    const std::size_t have = request.size() - head->headerBytes;
    if (have < head->contentLength) return plain(400, en ? "Bad request." : "Ungültige Anfrage.", "");
    auto code = extractCode(request.substr(head->headerBytes, head->contentLength));
    if (!code) return page(400, en ? "That does not look like a Dropbox code." : "Das sieht nicht nach einem Dropbox-Code aus.");
    code_ = std::move(code);
    consumed_ = true;
    return plain(200, en ? "Code sent. Look at the Wii." : "Code übermittelt. Weiter auf der Wii.", "ok");
}

std::optional<std::string> PairingSession::takeCode() {
    std::optional<std::string> c = std::move(code_);
    code_.reset();
    return c;
}

void PairingSession::rearm() {
    consumed_ = false;
    code_.reset();
}

}  // namespace nl
