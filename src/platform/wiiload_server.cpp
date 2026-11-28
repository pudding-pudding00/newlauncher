#include "platform/wiiload_server.hpp"

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

constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;
constexpr s32 kSliceMs = 100;
constexpr u32 kIosNonBlock = 4;
constexpr u32 kPollIn = 0x0001;
constexpr u64 kHeaderTimeoutMs = 10000;
constexpr u64 kPayloadTimeoutMs = 30000;
constexpr std::size_t kChunkBytes = 16 * 1024;

std::string formatIpv4(u32 netOrderIp) {
    const u32 ip = ntohl(netOrderIp);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u",
                  (ip >> 24) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 8) & 0xFFu, ip & 0xFFu);
    return buf;
}

const char* kindTag(WiiloadKind k) {
    switch (k) {
        case WiiloadKind::Elf: return "elf";
        case WiiloadKind::Zip: return "zip";
        case WiiloadKind::Dol: break;
    }
    return "dol";
}

}  // namespace

WiiloadServer::WiiloadServer() { LWP_MutexInit(&mutex_, false); }

WiiloadServer::~WiiloadServer() {
    stop();
    LWP_MutexDestroy(mutex_);
}

bool WiiloadServer::start(u16 port) {
    if (running() && port_ == port) return true;
    stop();
    port_ = port == 0 ? kWiiloadDefaultPort : port;
    stop_ = false;
    if (LWP_CreateThread(&thread_, &WiiloadServer::entry, this, nullptr, kStackBytes, kPriority) == 0) {
        return true;
    }
    thread_ = LWP_THREAD_NULL;
    return false;
}

void WiiloadServer::stop() {
    if (thread_ == LWP_THREAD_NULL) return;
    stop_ = true;
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
    LWP_MutexLock(mutex_);
    pending_.reset();
    LWP_MutexUnlock(mutex_);
    log("NL_WIILOAD stop");
}

std::optional<WiiloadPayload> WiiloadServer::take() {
    LWP_MutexLock(mutex_);
    std::optional<WiiloadPayload> out = std::move(pending_);
    pending_.reset();
    LWP_MutexUnlock(mutex_);
    return out;
}

std::vector<std::string> WiiloadServer::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void WiiloadServer::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void* WiiloadServer::entry(void* self) {
    static_cast<WiiloadServer*>(self)->loop();
    return nullptr;
}

bool WiiloadServer::readExact(s32 fd, u8* dst, std::size_t n, u64 timeoutMs) {
    std::size_t got = 0;
    const u64 deadline = NetService::clockMs() + timeoutMs;
    while (got < n && !stop_) {
        const u64 now = NetService::clockMs();
        if (now >= deadline) return false;
        pollsd p{fd, kPollIn, 0};
        const s32 waitMs = static_cast<s32>((deadline - now) < static_cast<u64>(kSliceMs) ? (deadline - now) : kSliceMs);
        const s32 pr = net_poll(&p, 1, waitMs);
        if (pr < 0) return false;
        if (pr == 0) continue;
        const std::size_t want = (n - got) < kChunkBytes ? (n - got) : kChunkBytes;
        const s32 r = net_read(fd, dst + got, want);
        if (r > 0) {
            got += static_cast<std::size_t>(r);
        } else if (r == 0) {
            return false;
        } else if (r != -EAGAIN && r != -EWOULDBLOCK) {
            return false;
        }
    }
    return got == n;
}

void WiiloadServer::serve(s32 fd, const std::string& senderIp) {
    u8 hdrBuf[kWiiloadHeaderBytes]{};
    if (!readExact(fd, hdrBuf, sizeof hdrBuf, kHeaderTimeoutMs)) {
        log("NL_WIILOAD reject header_timeout");
        return;
    }
    auto hdr = parseWiiloadHeader(hdrBuf);
    if (!hdr.isOk()) {
        log("NL_WIILOAD reject " + hdr.error().context);
        return;
    }

    std::vector<u8> raw(hdr.value().compSize);
    if (!readExact(fd, raw.data(), raw.size(), kPayloadTimeoutMs)) {
        log("NL_WIILOAD reject payload_timeout");
        return;
    }

    std::vector<u8> rawArgs(hdr.value().argsLen);
    if (!rawArgs.empty() && !readExact(fd, rawArgs.data(), rawArgs.size(), kHeaderTimeoutMs)) {
        log("NL_WIILOAD reject args_timeout");
        return;
    }

    auto decoded = decodeWiiloadPayload(hdr.value(), raw);
    if (!decoded.isOk()) {
        log("NL_WIILOAD reject " + decoded.error().context);
        return;
    }

    auto args = parseWiiloadArgs(rawArgs);
    const std::string_view argv0 = args.empty() ? std::string_view{} : std::string_view(args[0]);
    const WiiloadKind kind = detectWiiloadKind(decoded.value(), argv0);

    WiiloadPayload payload;
    payload.senderIp = senderIp;
    payload.kind = kind;
    payload.bytes = std::move(decoded.value());
    payload.args = std::move(args);

    char msg[128];
    std::snprintf(msg, sizeof msg, "NL_WIILOAD recv kind=%s size=%u",
                  kindTag(kind), static_cast<unsigned>(payload.bytes.size()));
    log(msg);

    LWP_MutexLock(mutex_);
    pending_ = std::move(payload);
    LWP_MutexUnlock(mutex_);
}

void WiiloadServer::loop() {
    while (!stop_) {
        if (listen_ < 0) {
            const s32 fd = net_gethostip() == 0 ? -1 : net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (fd >= 0) {
                const u32 one = 1;
                net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port_);
                addr.sin_addr.s_addr = INADDR_ANY;
                if (net_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && net_listen(fd, 1) == 0) {
                    net_fcntl(fd, F_SETFL, kIosNonBlock);
                    listen_ = fd;
                    log("NL_WIILOAD listen " + std::to_string(port_));
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
        serve(fd, formatIpv4(peer.sin_addr.s_addr));
        net_close(fd);
    }
    if (listen_ >= 0) net_close(listen_);
    listen_ = -1;
}

}  // namespace nl::platform
