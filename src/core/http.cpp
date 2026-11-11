#include "core/http.hpp"

#include <algorithm>
#include <cctype>

namespace nl {
namespace {

constexpr std::size_t kMaxHead = 8 * 1024;

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::optional<u32> parseDec(std::string_view s) {
    if (s.empty() || s.size() > 10) return std::nullopt;
    u64 v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + u64(c - '0');
    }
    if (v > 0xFFFFFFFFu) return std::nullopt;
    return u32(v);
}

std::optional<u32> parseHex(std::string_view s) {
    if (const auto semi = s.find(';'); semi != std::string_view::npos) s = s.substr(0, semi);
    s = trim(s);
    if (s.empty() || s.size() > 8) return std::nullopt;
    u32 v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        v = (v << 4) | u32(d);
    }
    return v;
}

}  // namespace

std::optional<Url> parseUrl(std::string_view url) {
    const auto sep = url.find("://");
    if (sep == std::string_view::npos) return std::nullopt;
    Url out;
    const std::string_view scheme = url.substr(0, sep);
    if (iequals(scheme, "https")) {
        out.tls = true;
        out.port = 443;
    } else if (!iequals(scheme, "http")) {
        return std::nullopt;
    }
    url.remove_prefix(sep + 3);
    const auto slash = url.find('/');
    std::string_view hostPort = url.substr(0, slash);
    out.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    if (const auto colon = hostPort.find(':'); colon != std::string_view::npos) {
        const auto port = parseDec(hostPort.substr(colon + 1));
        if (!port || *port == 0 || *port > 65535) return std::nullopt;
        out.port = u16(*port);
        hostPort = hostPort.substr(0, colon);
    }
    if (hostPort.empty()) return std::nullopt;
    out.host = std::string(hostPort);
    return out;
}

std::string buildGetRequest(const Url& url) {
    std::string r = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host;
    if (url.port != (url.tls ? 443 : 80)) r += ":" + std::to_string(url.port);
    r += "\r\nUser-Agent: newlauncher\r\nConnection: close\r\n\r\n";
    return r;
}

bool HttpParser::parseHead() {
    std::string_view head = head_;
    const auto eol = head.find("\r\n");
    const std::string_view statusLine = head.substr(0, eol);
    // "HTTP/1.x NNN ..."
    if (statusLine.size() < 12 || statusLine.substr(0, 7) != "HTTP/1." || statusLine[8] != ' ') return false;
    const auto code = parseDec(statusLine.substr(9, 3));
    if (!code || (statusLine.size() > 12 && statusLine[12] != ' ')) return false;
    status_ = int(*code);

    std::size_t at = eol + 2;
    while (at < head.size()) {
        const auto end = head.find("\r\n", at);
        const std::string_view line = head.substr(at, end - at);
        at = end == std::string_view::npos ? head.size() : end + 2;
        if (line.empty()) break;
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) return false;
        const std::string_view name = trim(line.substr(0, colon)), value = trim(line.substr(colon + 1));
        if (iequals(name, "content-length")) {
            length_ = parseDec(value);
            if (!length_) return false;
        } else if (iequals(name, "transfer-encoding")) {
            std::string v(value);
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            chunked_ = v.find("chunked") != std::string::npos;
        }
    }
    return true;
}

bool HttpParser::append(const u8* p, std::size_t n) {
    if (u64{received_} + n > maxBody_) return false;
    received_ += u32(n);
    if (n == 0) return true;
    if (sink_) return sink_(std::span<const u8>(p, n));
    body_.insert(body_.end(), p, p + n);
    return true;
}

HttpParser::State HttpParser::feed(std::span<const u8> data) {
    if (state_ == State::Error || state_ == State::Done) return state_;
    const u8* p = data.data();
    std::size_t n = data.size();
    if (state_ == State::Headers) {
        const std::size_t before = head_.size();
        head_.append(reinterpret_cast<const char*>(p), n);
        const auto end = head_.find("\r\n\r\n", before >= 3 ? before - 3 : 0);
        if (end == std::string::npos) {
            if (head_.size() > kMaxHead) return fail();
            return state_;
        }
        if (end + 4 > kMaxHead) return fail();
        const std::size_t consumed = end + 4 - before;  // Bytes dieses Stücks, die zum Kopf gehören
        head_.resize(end + 4);
        if (!parseHead()) return fail();
        if (!chunked_ && length_ && *length_ > maxBody_) return fail();
        state_ = State::Body;
        p += consumed;
        n -= consumed;
        if (!chunked_ && length_ && *length_ == 0) return state_ = State::Done;
    }
    return feedBody(p, n);
}

HttpParser::State HttpParser::feedBody(const u8* p, std::size_t n) {
    if (!chunked_) {
        if (length_) n = std::min<std::size_t>(n, *length_ - received_);
        if (!append(p, n)) return fail();
        if (length_ && received_ == *length_) state_ = State::Done;
        return state_;
    }
    while (n > 0 && state_ == State::Body) {
        switch (chunk_) {
        case Chunk::Size:
        case Chunk::Trailer:
        case Chunk::DataEnd: {
            const char c = char(*p++);
            --n;
            line_ += c;
            if (line_.size() > 256) return fail();
            if (line_.size() < 2 || line_.compare(line_.size() - 2, 2, "\r\n") != 0) break;
            const std::string_view line(line_.data(), line_.size() - 2);
            if (chunk_ == Chunk::DataEnd) {
                if (!line.empty()) return fail();
                chunk_ = Chunk::Size;
            } else if (chunk_ == Chunk::Trailer) {
                if (line.empty()) state_ = State::Done;
            } else {
                const auto size = parseHex(line);
                if (!size) return fail();
                if (*size == 0) chunk_ = Chunk::Trailer;
                else if (u64{received_} + *size > maxBody_) return fail();
                else chunkLeft_ = *size, chunk_ = Chunk::Data;
            }
            line_.clear();
            break;
        }
        case Chunk::Data: {
            const std::size_t take = std::min<std::size_t>(n, chunkLeft_);
            if (!append(p, take)) return fail();
            p += take;
            n -= take;
            chunkLeft_ -= u32(take);
            if (chunkLeft_ == 0) chunk_ = Chunk::DataEnd;
            break;
        }
        }
    }
    return state_;
}

HttpParser::State HttpParser::finish() {
    if (state_ == State::Body && !chunked_ && !length_) return state_ = State::Done;
    if (state_ != State::Done) return fail();
    return state_;
}

}  // namespace nl
