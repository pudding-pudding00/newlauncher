#include "platform/http_client.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>

#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

namespace nl::platform {

namespace {
constexpr s32 kTimeoutMs = 10000;
constexpr s32 kSliceMs = 100;  // Abbruch-Prüfung beim Warten
constexpr u32 kIosNonBlock = 4;  // IOS-Wert für O_NONBLOCK; libogc reicht net_fcntl-Flags unverändert durch
constexpr s32 kPollIn = 0x0001, kPollOut = 0x0008;

bool isIpLiteral(const std::string& host) {
    if (host.empty()) return false;
    for (char c : host)
        if ((c < '0' || c > '9') && c != '.') return false;
    return true;
}

bool resolve(const std::string& host, in_addr& out) {
    if (isIpLiteral(host)) return inet_aton(host.c_str(), &out) != 0;
    hostent* h = net_gethostbyname(host.c_str());
    if (h == nullptr || h->h_addr_list == nullptr || h->h_addr_list[0] == nullptr) return false;
    std::memcpy(&out, h->h_addr_list[0], sizeof out);
    return true;
}

u64 elapsedMs(u64 start) { return ticks_to_millisecs(diff_ticks(start, gettime())); }

bool isCancelled(const volatile bool* cancel) { return cancel != nullptr && *cancel; }

// Wartet auf ein Ereignis; false bei Timeout, Fehler oder Abbruch.
bool waitFor(s32 sock, u32 events, const volatile bool* cancel) {
    const u64 start = gettime();
    while (!isCancelled(cancel)) {
        pollsd p{sock, events, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r > 0) return (p.revents & events) != 0;
        if (r < 0 || elapsedMs(start) > static_cast<u64>(kTimeoutMs)) return false;
    }
    return false;
}

bool connectWithTimeout(s32 sock, const sockaddr_in& addr, const volatile bool* cancel) {
    const u64 start = gettime();
    for (;;) {
        if (isCancelled(cancel)) return false;
        const s32 r = net_connect(sock, reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(&addr)), sizeof addr);
        if (r >= 0 || r == -EISCONN) return true;
        if (r != -EINPROGRESS && r != -EALREADY && r != -EAGAIN) return false;
        if (elapsedMs(start) > static_cast<u64>(kTimeoutMs)) return false;
        usleep(20 * 1000);
    }
}

bool sendAll(s32 sock, const std::string& data, const volatile bool* cancel) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const s32 r = net_send(sock, data.data() + sent, static_cast<s32>(data.size() - sent), 0);
        if (r > 0) {
            sent += static_cast<std::size_t>(r);
        } else if (r == -EAGAIN) {
            if (!waitFor(sock, kPollOut, cancel)) return false;
        } else {
            return false;
        }
    }
    return true;
}

struct Socket {
    s32 fd = -1;
    ~Socket() {
        if (fd >= 0) net_close(fd);
    }
};

bool g_netReady = false;

// Verbinden, Anfrage senden, Antwort in den Parser füttern. Ok/NotFound/Failed nach Status.
HttpOutcome request(const Url& url, HttpParser& parser, const volatile bool* cancel) {
    in_addr ip{};
    if (!resolve(url.host, ip)) return HttpOutcome::Failed;

    Socket sock;
    sock.fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock.fd < 0) return HttpOutcome::Failed;
    net_fcntl(sock.fd, F_SETFL, kIosNonBlock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(url.port);
    addr.sin_addr = ip;
    if (!connectWithTimeout(sock.fd, addr, cancel)) return HttpOutcome::Failed;
    if (!sendAll(sock.fd, buildGetRequest(url), cancel)) return HttpOutcome::Failed;

    u8 buf[4096];
    HttpParser::State state = HttpParser::State::Headers;
    while (state == HttpParser::State::Headers || state == HttpParser::State::Body) {
        const s32 r = net_recv(sock.fd, buf, sizeof buf, 0);
        if (r > 0) {
            state = parser.feed(std::span<const u8>(buf, static_cast<std::size_t>(r)));
        } else if (r == 0) {
            state = parser.finish();
        } else if (r == -EAGAIN) {
            if (!waitFor(sock.fd, kPollIn, cancel)) return HttpOutcome::Failed;
        } else {
            return HttpOutcome::Failed;
        }
    }
    if (state != HttpParser::State::Done) return HttpOutcome::Failed;
    if (parser.status() == 404) return HttpOutcome::NotFound;
    if (parser.status() != 200) return HttpOutcome::Failed;
    return HttpOutcome::Ok;
}

struct FileSink {
    static constexpr std::size_t kBuf = 64 * 1024;
    std::FILE* f = nullptr;
    std::vector<u8> buf;
    bool ok = true;
    bool flush() {
        if (!buf.empty() && std::fwrite(buf.data(), 1, buf.size(), f) != buf.size()) ok = false;
        buf.clear();
        return ok;
    }
    bool put(std::span<const u8> data) {
        if (buf.size() + data.size() > kBuf && !flush()) return false;
        buf.insert(buf.end(), data.begin(), data.end());
        return true;
    }
};
}  // namespace

HttpOutcome httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel) {
    body.clear();
    HttpParser parser(maxBody);
    const HttpOutcome r = request(url, parser, cancel);
    if (r == HttpOutcome::Ok) body.assign(parser.body().begin(), parser.body().end());
    return r;
}

HttpOutcome httpDownload(const Url& url, const std::string& destFile, u32 maxBody, const volatile bool* cancel,
                         volatile u32* received) {
    FileSink sink;
    sink.f = std::fopen(destFile.c_str(), "wb");
    if (sink.f == nullptr) return HttpOutcome::Failed;
    sink.buf.reserve(FileSink::kBuf);
    HttpParser parser(maxBody);
    parser.setSink([&](std::span<const u8> data) {
        if (received != nullptr) *received = *received + static_cast<u32>(data.size());
        return sink.put(data);
    });
    HttpOutcome r = request(url, parser, cancel);
    // Ein Fehlerstatus (404 o. ä.) liefert eine Fehlerseite in die Senke; die Datei wird unten gelöscht.
    if (!sink.flush()) r = HttpOutcome::Failed;
    if (std::fclose(sink.f) != 0) r = HttpOutcome::Failed;
    if (r != HttpOutcome::Ok) std::remove(destFile.c_str());
    return r;
}

bool ensureNetwork(s32& code) {
    if (g_netReady) return true;
    s32 r = -1;
    for (int i = 0; i < 3 && r < 0; ++i) {
        r = net_init();
        if (r < 0) usleep(500 * 1000);
    }
    code = r;
    if (r < 0) return false;
    g_netReady = true;
    return true;
}

}  // namespace nl::platform
