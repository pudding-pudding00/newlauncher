#include "platform/http_client.hpp"

#include <cerrno>
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
}  // namespace

HttpOutcome httpGet(const Url& url, u32 maxBody, std::vector<u8>& body, const volatile bool* cancel) {
    body.clear();
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

    HttpParser parser(maxBody);
    static u8 buf[4096];  // nur der Netz-Thread ruft httpGet auf
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
    body.assign(parser.body().begin(), parser.body().end());
    return HttpOutcome::Ok;
}

}  // namespace nl::platform
