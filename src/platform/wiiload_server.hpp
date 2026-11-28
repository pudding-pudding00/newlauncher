#pragma once
// wiiload v0.5 TCP receiver on port 4299 (M31): runs in a dedicated LWP thread using plain libogc sockets.

#include <optional>
#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/types.hpp"
#include "core/wiiload_proto.hpp"

namespace nl::platform {

struct WiiloadPayload {
    std::string senderIp;
    WiiloadKind kind = WiiloadKind::Dol;
    std::vector<u8> bytes;
    std::vector<std::string> args;
};

class WiiloadServer {
public:
    WiiloadServer();
    ~WiiloadServer();
    WiiloadServer(const WiiloadServer&) = delete;
    WiiloadServer& operator=(const WiiloadServer&) = delete;

    bool start(u16 port = kWiiloadDefaultPort);
    void stop();
    bool running() const { return thread_ != LWP_THREAD_NULL; }
    u16 port() const { return port_; }

    std::optional<WiiloadPayload> take();
    std::vector<std::string> takeLogLines();

private:
    static void* entry(void* self);
    void loop();
    void serve(s32 fd, const std::string& senderIp);
    bool readExact(s32 fd, u8* dst, std::size_t n, u64 timeoutMs);
    void log(std::string line);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutable mutex_t mutex_ = LWP_MUTEX_NULL;
    volatile bool stop_ = false;
    u16 port_ = kWiiloadDefaultPort;
    s32 listen_ = -1;
    std::optional<WiiloadPayload> pending_;
    std::vector<std::string> logLines_;
};

}  // namespace nl::platform
