#include "platform/pairing_server.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <utility>

#include <network.h>
#include <unistd.h>

#include "platform/net_service.hpp"

namespace nl::platform {

namespace {
constexpr u32 kStackBytes = 16 * 1024;
constexpr u8 kPriority = 40;     // unter dem Haupt-Thread, wie RemoteServer
constexpr s32 kSliceMs = 100;    // Abbruch-Prüfung beim Warten
constexpr u32 kIosNonBlock = 4;  // IOS-Wert für O_NONBLOCK
constexpr u32 kPollIn = 0x0001;
constexpr u64 kConnTimeoutMs = 5000;
alignas(32) u8 g_sendBuf[2048];  // ausgerichteter Zwischenpuffer wie in RemoteServer (nur dieser Thread)
}  // namespace

std::string pairingUrl(std::string_view token) {
    const u32 ip = net_gethostip();
    if (ip == 0) return std::string();
    char head[40];
    std::snprintf(head, sizeof head, "http://%u.%u.%u.%u:%u/", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff,
                  ip & 0xff, static_cast<unsigned>(kPairPort));
    return head + std::string(token);
}

PairingServer::PairingServer() { LWP_MutexInit(&mutex_, false); }

PairingServer::~PairingServer() {
    stop();
    LWP_MutexDestroy(mutex_);
}

bool PairingServer::start(std::string token, std::string authorizeUrl, bool english) {
    stop();
    LWP_MutexLock(mutex_);
    session_.emplace(std::move(token), std::move(authorizeUrl), NetService::clockMs(), english);
    LWP_MutexUnlock(mutex_);
    stop_ = false;
    if (LWP_CreateThread(&thread_, &PairingServer::entry, this, nullptr, kStackBytes, kPriority) == 0) return true;
    thread_ = LWP_THREAD_NULL;
    return false;
}

void PairingServer::stop() {
    if (thread_ == LWP_THREAD_NULL) return;
    stop_ = true;
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
    LWP_MutexLock(mutex_);
    session_.reset();
    LWP_MutexUnlock(mutex_);
    log("NL_PAIR stop");
}

std::optional<std::string> PairingServer::takeCode() {
    LWP_MutexLock(mutex_);
    std::optional<std::string> c = session_ ? session_->takeCode() : std::nullopt;
    LWP_MutexUnlock(mutex_);
    return c;
}

void PairingServer::rearm() {
    LWP_MutexLock(mutex_);
    if (session_) session_->rearm();
    LWP_MutexUnlock(mutex_);
}

std::vector<std::string> PairingServer::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void PairingServer::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void* PairingServer::entry(void* self) {
    static_cast<PairingServer*>(self)->loop();
    return nullptr;
}

void PairingServer::loop() {
    while (!stop_) {
        if (listen_ < 0) {
            const s32 fd = net_gethostip() == 0 ? -1 : net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (fd >= 0) {
                const u32 one = 1;
                net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(kPairPort);
                addr.sin_addr.s_addr = INADDR_ANY;
                if (net_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && net_listen(fd, 1) == 0) {
                    net_fcntl(fd, F_SETFL, kIosNonBlock);
                    listen_ = fd;
                    log("NL_PAIR listen " + std::to_string(kPairPort));
                } else {
                    net_close(fd);
                }
            }
            if (listen_ < 0) {
                for (int i = 0; i < 10 && !stop_; ++i) usleep(kSliceMs * 1000);
                continue;
            }
        }
        pollsd p{listen_, kPollIn, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r < 0) {
            net_close(listen_);
            listen_ = -1;
            continue;
        }
        if (r == 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        const s32 fd = net_accept(listen_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) continue;
        net_fcntl(fd, F_SETFL, kIosNonBlock);
        serve(fd);
        net_close(fd);
    }
    if (listen_ >= 0) net_close(listen_);
    listen_ = -1;
}

// Liest, bis Kopf und Body vollständig sind (oder die Grenze überschritten ist), beantwortet und schließt.
void PairingServer::serve(s32 fd) {
    std::string req;
    const u64 deadline = NetService::clockMs() + kConnTimeoutMs;
    for (;;) {
        if (req.size() > kPairMaxRequest) break;  // handle() antwortet 413
        if (req.find("\r\n\r\n") != std::string::npos) {
            const auto head = parseHttpHead(req);
            if (!head || head->headerBytes + head->contentLength > kPairMaxRequest ||
                req.size() >= head->headerBytes + head->contentLength)
                break;
        }
        if (stop_ || NetService::clockMs() >= deadline) return;  // ohne Antwort schließen
        char buf[512];
        const s32 n = net_recv(fd, buf, sizeof buf, 0);
        if (n > 0) {
            req.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) break;  // Gegenseite fertig: was da ist, beantworten (kaputter Kopf → 400)
        if (n != -EAGAIN) return;
        pollsd p{fd, kPollIn, 0};
        if (net_poll(&p, 1, kSliceMs) < 0) return;
    }
    LWP_MutexLock(mutex_);
    PairingReply reply;
    const bool have = session_.has_value();
    if (have) reply = session_->handle(req, NetService::clockMs());
    LWP_MutexUnlock(mutex_);
    if (!have) return;
    if (reply.log && !reply.marker.empty()) log("NL_PAIR " + reply.marker);
    const std::string out = buildPairingResponse(reply);
    net_fcntl(fd, F_SETFL, 0);  // blockierend senden (siehe RemoteServer::sendAll); Antworten sind klein
    std::size_t done = 0;
    while (done < out.size()) {
        const std::size_t want = out.size() - done < sizeof g_sendBuf ? out.size() - done : sizeof g_sendBuf;
        std::memcpy(g_sendBuf, out.data() + done, want);
        const s32 w = net_send(fd, g_sendBuf, static_cast<s32>(want), 0);
        if (w <= 0) break;
        done += static_cast<std::size_t>(w);
    }
}

}  // namespace nl::platform
