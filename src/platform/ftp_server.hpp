#pragma once
// Passive-mode RFC 959 FTP server for SD and USB file transfers (M32).
// Runs in a dedicated LWP thread (64 KiB stack) with strict 3-socket IOS budget and 32 KiB streaming buffer.

#include <string>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include "core/ftp_proto.hpp"
#include "core/ftp_vfs.hpp"
#include "core/types.hpp"

namespace nl::platform {

struct FtpStatus {
    bool listening = false;
    bool clientConnected = false;
    bool transferring = false;
    bool dirty = false;
    u32 hostIp = 0;
    u16 port = kFtpDefaultPort;
    std::string peerIp;
    std::string activeOp;
    std::string activePath;
    u64 activeBytes = 0;
};

class FtpServer {
public:
    FtpServer();
    ~FtpServer();
    FtpServer(const FtpServer&) = delete;
    FtpServer& operator=(const FtpServer&) = delete;

    bool start(u16 port, std::string user, std::string pass, std::vector<FtpMount> mounts);
    void stop();
    bool running() const { return thread_ != LWP_THREAD_NULL; }

    FtpStatus status() const;
    std::vector<std::string> takeLogLines();

private:
    static void* entry(void* self);
    void loop();
    void handleClient(s32 fd, const std::string& peerIp);
    void sendLine(s32 fd, std::string_view line);
    bool openPasv(s32& pasvFd, u16& outPort);
    s32 acceptPasv(s32& pasvFd);
    void setTransferState(bool active, std::string op, std::string path, u64 bytes);
    void log(std::string line);

    lwp_t thread_ = LWP_THREAD_NULL;
    mutable mutex_t mutex_ = LWP_MUTEX_NULL;
    volatile bool stop_ = false;
    u16 port_ = kFtpDefaultPort;
    std::string user_;
    std::string pass_;
    std::vector<FtpMount> mounts_;
    FtpStatus status_;
    std::vector<std::string> logLines_;
};

}  // namespace nl::platform
