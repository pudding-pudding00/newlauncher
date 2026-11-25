#include "platform/remote_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <utility>

#include <network.h>
#include <unistd.h>

#include "core/crc32.hpp"
#include "platform/log.hpp"
#include "ui/input.hpp"

namespace nl::platform {

// core/remote_proto kennt ui nicht; die Tastenbits müssen trotzdem dieselben sein.
static_assert(RawButtons::A == ui::RawInput::A && RawButtons::GcStart == ui::RawInput::GcStart &&
              RawButtons::HomeBtn == ui::RawInput::HomeBtn && RawButtons::DRight == ui::RawInput::DRight);

namespace {
constexpr u32 kStackBytes = 32 * 1024;
constexpr u8 kPriority = 40;              // unter dem Haupt-Thread, wie NetService
constexpr s32 kSliceMs = 100;             // Abbruch-Prüfung beim Warten
constexpr u32 kIosNonBlock = 4;           // IOS-Wert für O_NONBLOCK (wie http_client.cpp)
constexpr u32 kPollIn = 0x0001, kPollOut = 0x0008;
constexpr u32 kIdleTimeoutSlices = 600;   // 60 s ohne Daten: Verbindung schließen
}  // namespace

RemoteServer::RemoteServer() {
    LWP_MutexInit(&mutex_, false);
    LWP_CondInit(&cond_);
}

RemoteServer::~RemoteServer() {
    stop();
    LWP_CondDestroy(cond_);
    LWP_MutexDestroy(mutex_);
}

bool RemoteServer::start() {
    if (thread_ != LWP_THREAD_NULL) return true;
    stop_ = false;
    if (LWP_CreateThread(&thread_, &RemoteServer::entry, this, nullptr, kStackBytes, kPriority) == 0) return true;
    thread_ = LWP_THREAD_NULL;
    return false;
}

void RemoteServer::stop() {
    if (thread_ == LWP_THREAD_NULL) return;
    LWP_MutexLock(mutex_);
    stop_ = true;
    LWP_CondBroadcast(cond_);
    LWP_MutexUnlock(mutex_);
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
}

bool RemoteServer::take(RemoteRequest& out, std::vector<u8>& data) {
    LWP_MutexLock(mutex_);
    const bool got = state_ == State::Pending;
    if (got) {
        out = std::move(req_);
        data = std::move(data_);
        state_ = State::Taken;
    }
    LWP_MutexUnlock(mutex_);
    return got;
}

void RemoteServer::reply(bool ok, std::string info, std::vector<u8> payload) {
    LWP_MutexLock(mutex_);
    if (state_ == State::Taken) {
        replyOk_ = ok;
        replyInfo_ = std::move(info);
        replyPayload_ = std::move(payload);
        state_ = State::Replied;
        LWP_CondBroadcast(cond_);
    }
    LWP_MutexUnlock(mutex_);
}

void* RemoteServer::entry(void* self) {
    static_cast<RemoteServer*>(self)->loop();
    return nullptr;
}

bool RemoteServer::openListen() {
    const s32 fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return false;
    const u32 one = 1;
    net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kRemotePort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (net_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || net_listen(fd, 1) < 0) {
        net_close(fd);
        return false;
    }
    net_fcntl(fd, F_SETFL, kIosNonBlock);
    listen_ = fd;
    const u32 ip = net_gethostip();
    log::line("NL_REMOTE listen %u.%u.%u.%u:%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
              static_cast<unsigned>(kRemotePort));
    return true;
}

void RemoteServer::closeListen() {
    if (listen_ >= 0) net_close(listen_);
    listen_ = -1;
}

void RemoteServer::loop() {
    while (!stop_) {
        if (listen_ < 0) {
            // Das Netz fährt der NetService hoch (WLAN-Probe nach dem Start); bis dahin nur warten.
            if (net_gethostip() == 0 || !openListen()) {
                for (int i = 0; i < 10 && !stop_; ++i) usleep(kSliceMs * 1000);
                continue;
            }
        }
        pollsd p{listen_, kPollIn, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r < 0) {
            closeListen();  // z. B. Netz neu aufgebaut: später neu binden
            continue;
        }
        if (r == 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        const s32 fd = net_accept(listen_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) continue;
        net_fcntl(fd, F_SETFL, kIosNonBlock);
        log::line("NL_REMOTE connect");
        pending_.clear();
        serve(fd);
        net_close(fd);
        log::line("NL_REMOTE disconnect");
    }
    closeListen();
}

bool RemoteServer::waitReady(s32 fd, u32 events) {
    for (u32 slices = 0; !stop_ && slices < kIdleTimeoutSlices; ++slices) {
        pollsd p{fd, events, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r < 0) return false;
        if (r > 0) return (p.revents & events) != 0;
    }
    return false;
}

bool RemoteServer::recvLine(s32 fd, std::string& line) {
    for (;;) {
        if (const std::size_t nl = pending_.find('\n'); nl != std::string::npos) {
            line = pending_.substr(0, nl);
            pending_.erase(0, nl + 1);
            return true;
        }
        if (pending_.size() > kRemoteMaxLine) return false;
        char buf[256];
        const s32 r = net_recv(fd, buf, sizeof buf, 0);
        if (r > 0) {
            pending_.append(buf, static_cast<std::size_t>(r));
            continue;
        }
        if (r == 0 || r != -EAGAIN) return false;
        if (!waitReady(fd, kPollIn)) return false;
    }
}

bool RemoteServer::recvExact(s32 fd, u8* out, u32 size) {
    const u32 fromPending = static_cast<u32>(std::min<std::size_t>(pending_.size(), size));
    std::memcpy(out, pending_.data(), fromPending);
    pending_.erase(0, fromPending);
    u32 got = fromPending;
    while (got < size) {
        const u32 want = std::min<u32>(size - got, 32 * 1024);
        const s32 r = net_recv(fd, out + got, static_cast<s32>(want), 0);
        if (r > 0) {
            got += static_cast<u32>(r);
            continue;
        }
        if (r == 0 || r != -EAGAIN) return false;
        if (!waitReady(fd, kPollIn)) return false;
    }
    return true;
}

bool RemoteServer::sendAll(s32 fd, const u8* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        const s32 want = static_cast<s32>(std::min<std::size_t>(size - done, 32 * 1024));
        const s32 r = net_send(fd, data + done, want, 0);
        if (r > 0) {
            done += static_cast<std::size_t>(r);
            continue;
        }
        if (r != -EAGAIN) return false;
        if (!waitReady(fd, kPollOut)) return false;
    }
    return true;
}

void RemoteServer::serve(s32 fd) {
    std::string line;
    while (!stop_ && recvLine(fd, line)) {
        auto parsed = parseRemoteLine(line);
        bool ok = false;
        std::string info;
        std::vector<u8> payload;
        if (!parsed.isOk()) {
            info = parsed.error().context;
        } else {
            RemoteRequest req = std::move(parsed).value();
            std::vector<u8> data;
            bool ready = true;
            if (req.cmd == RemoteCmd::Put) {
                data.resize(req.size);
                if (!recvExact(fd, data.data(), req.size)) return;
                if (crc32(data) != req.crc) {
                    ready = false;
                    info = "CRC passt nicht";
                }
            }
            if (ready) {
                LWP_MutexLock(mutex_);
                req_ = std::move(req);
                data_ = std::move(data);
                state_ = State::Pending;
                while (!stop_ && state_ != State::Replied) LWP_CondWait(cond_, mutex_);
                if (stop_) {
                    state_ = State::Idle;
                    LWP_MutexUnlock(mutex_);
                    return;
                }
                ok = replyOk_;
                info = std::move(replyInfo_);
                payload = std::move(replyPayload_);
                state_ = State::Idle;
                LWP_MutexUnlock(mutex_);
            }
        }
        const std::string head = remoteReplyHeader(ok, payload.size(), info);
        if (!sendAll(fd, reinterpret_cast<const u8*>(head.data()), head.size())) return;
        if (!payload.empty() && !sendAll(fd, payload.data(), payload.size())) return;
        sent_ = sent_ + 1;
    }
}

}  // namespace nl::platform
