#include "platform/ftp_server.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include <network.h>

#include "platform/net_service.hpp"

namespace nl::platform {
namespace {

constexpr u32 kStackBytes = 64 * 1024;
constexpr u8 kPriority = 40;
constexpr s32 kSliceMs = 100;
constexpr u32 kIosNonBlock = 4;
constexpr u32 kPollIn = 0x0001;
constexpr u64 kPasvTimeoutMs = 15000;
constexpr u64 kIdleTimeoutMs = 120000;
alignas(32) u8 g_ftpBuf[32 * 1024];

std::string formatIpv4(u32 netOrderIp) {
    const u32 ip = ntohl(netOrderIp);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u",
                  (ip >> 24) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 8) & 0xFFu, ip & 0xFFu);
    return buf;
}

bool sendAll(s32 fd, const u8* data, std::size_t len, volatile bool& stopFlag) {
    std::size_t sent = 0;
    while (sent < len && !stopFlag) {
        const std::size_t chunk = (len - sent) < 16384u ? (len - sent) : 16384u;
        const s32 w = net_write(fd, data + sent, chunk);
        if (w > 0) {
            sent += static_cast<std::size_t>(w);
        } else if (w == -EAGAIN || w == -EWOULDBLOCK) {
            usleep(10 * 1000);
        } else {
            return false;
        }
    }
    return sent == len;
}

bool readLine(s32 fd, std::string& out, volatile bool& stopFlag, u64 timeoutMs) {
    out.clear();
    const u64 deadline = NetService::clockMs() + timeoutMs;
    while (!stopFlag) {
        const u64 now = NetService::clockMs();
        if (now >= deadline) return false;
        pollsd p{fd, kPollIn, 0};
        const s32 pr = net_poll(&p, 1, kSliceMs);
        if (pr < 0) return false;
        if (pr == 0) continue;
        char c = 0;
        const s32 r = net_read(fd, &c, 1);
        if (r == 1) {
            if (c == '\n') return true;
            if (out.size() < 1024) out.push_back(c);
        } else if (r == 0) {
            return false;
        } else if (r != -EAGAIN && r != -EWOULDBLOCK) {
            return false;
        }
    }
    return false;
}

}  // namespace

FtpServer::FtpServer() { LWP_MutexInit(&mutex_, false); }

FtpServer::~FtpServer() {
    stop();
    LWP_MutexDestroy(mutex_);
}

bool FtpServer::start(u16 port, std::string user, std::string pass, std::vector<FtpMount> mounts) {
    stop();
    port_ = port == 0 ? kFtpDefaultPort : port;
    user_ = std::move(user);
    pass_ = std::move(pass);
    mounts_ = std::move(mounts);
    LWP_MutexLock(mutex_);
    status_ = FtpStatus{};
    status_.port = port_;
    LWP_MutexUnlock(mutex_);
    stop_ = false;
    if (LWP_CreateThread(&thread_, &FtpServer::entry, this, nullptr, kStackBytes, kPriority) == 0) {
        return true;
    }
    thread_ = LWP_THREAD_NULL;
    return false;
}

void FtpServer::stop() {
    if (thread_ == LWP_THREAD_NULL) return;
    stop_ = true;
    LWP_JoinThread(thread_, nullptr);
    thread_ = LWP_THREAD_NULL;
    bool dirty = false;
    LWP_MutexLock(mutex_);
    dirty = status_.dirty;
    status_.listening = false;
    status_.clientConnected = false;
    status_.transferring = false;
    LWP_MutexUnlock(mutex_);
    log(std::string("NL_FTP stop dirty=") + (dirty ? "1" : "0"));
}

FtpStatus FtpServer::status() const {
    LWP_MutexLock(mutex_);
    FtpStatus copy = status_;
    LWP_MutexUnlock(mutex_);
    return copy;
}

std::vector<std::string> FtpServer::takeLogLines() {
    LWP_MutexLock(mutex_);
    std::vector<std::string> out = std::move(logLines_);
    logLines_.clear();
    LWP_MutexUnlock(mutex_);
    return out;
}

void FtpServer::log(std::string line) {
    LWP_MutexLock(mutex_);
    logLines_.push_back(std::move(line));
    LWP_MutexUnlock(mutex_);
}

void FtpServer::setTransferState(bool active, std::string op, std::string path, u64 bytes) {
    LWP_MutexLock(mutex_);
    status_.transferring = active;
    status_.activeOp = std::move(op);
    status_.activePath = std::move(path);
    status_.activeBytes = bytes;
    LWP_MutexUnlock(mutex_);
}

void* FtpServer::entry(void* self) {
    static_cast<FtpServer*>(self)->loop();
    return nullptr;
}

void FtpServer::sendLine(s32 fd, std::string_view line) {
    std::string msg(line);
    msg += "\r\n";
    sendAll(fd, reinterpret_cast<const u8*>(msg.data()), msg.size(), stop_);
}

bool FtpServer::openPasv(s32& pasvFd, u16& outPort) {
    if (pasvFd >= 0) {
        net_close(pasvFd);
        pasvFd = -1;
    }
    const s32 fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return false;
    const u32 one = 1;
    net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    // Try ephemeral port range 50000..50099.
    for (u16 p = 50000; p < 50100; ++p) {
        addr.sin_port = htons(p);
        if (net_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && net_listen(fd, 1) == 0) {
            net_fcntl(fd, F_SETFL, kIosNonBlock);
            pasvFd = fd;
            outPort = p;
            return true;
        }
    }
    net_close(fd);
    return false;
}

s32 FtpServer::acceptPasv(s32& pasvFd) {
    if (pasvFd < 0) return -1;
    const u64 deadline = NetService::clockMs() + kPasvTimeoutMs;
    s32 dataFd = -1;
    while (!stop_ && NetService::clockMs() < deadline) {
        pollsd p{pasvFd, kPollIn, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r < 0) break;
        if (r == 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        dataFd = net_accept(pasvFd, reinterpret_cast<sockaddr*>(&peer), &len);
        if (dataFd >= 0) {
            net_fcntl(dataFd, F_SETFL, kIosNonBlock);
            break;
        }
    }
    net_close(pasvFd);
    pasvFd = -1;
    return dataFd;
}

void FtpServer::handleClient(s32 fd, const std::string& peerIp) {
    FtpSession session;
    session.setCredentials(user_, pass_);
    s32 pasvFd = -1;

    LWP_MutexLock(mutex_);
    status_.clientConnected = true;
    status_.peerIp = peerIp;
    LWP_MutexUnlock(mutex_);

    sendLine(fd, "220 newlauncher FTP ready.");
    std::string line;
    while (!stop_ && readLine(fd, line, stop_, kIdleTimeoutMs)) {
        auto parsed = parseFtpCommand(line);
        if (!parsed) {
            sendLine(fd, "500 Syntax error.");
            continue;
        }
        const std::string& verb = parsed->verb;
        const std::string& arg = parsed->arg;

        if (verb == "QUIT") {
            sendLine(fd, "221 Goodbye.");
            break;
        }
        if (verb == "NOOP") {
            sendLine(fd, "200 OK.");
            continue;
        }
        if (verb == "SYST") {
            sendLine(fd, "215 UNIX Type: L8");
            continue;
        }
        if (verb == "FEAT") {
            sendLine(fd, "211-Features:\r\n UTF8\r\n SIZE\r\n REST STREAM\r\n MLSD\r\n211 End");
            continue;
        }
        if (verb == "OPTS") {
            sendLine(fd, "200 OK.");
            continue;
        }
        if (verb == "USER") {
            session.setPendingUser(arg);
            sendLine(fd, "331 Password required.");
            continue;
        }
        if (verb == "PASS") {
            const auto res = session.authenticate(arg, NetService::clockMs());
            if (res == FtpAuthResult::Ok) {
                sendLine(fd, "230 Login successful.");
                log("NL_FTP login ok");
            } else if (res == FtpAuthResult::LockedOut) {
                sendLine(fd, "421 Too many failed logins.");
                log("NL_FTP login locked");
                break;
            } else {
                sendLine(fd, "530 Login incorrect.");
            }
            continue;
        }
        if (!session.loggedIn()) {
            sendLine(fd, "530 Please login with USER and PASS.");
            continue;
        }
        if (verb == "TYPE") {
            sendLine(fd, "200 Type set.");
            continue;
        }
        if (verb == "PORT" || verb == "EPRT") {
            sendLine(fd, "502 Active mode unsupported; use PASV.");
            continue;
        }
        if (verb == "PWD" || verb == "XPWD") {
            sendLine(fd, session.formatPwdReply());
            continue;
        }
        if (verb == "CDUP") {
            auto p = resolveFtpPath(session.cwd(), "..", mounts_);
            if (p.isOk()) {
                session.setCwd(p.value().virtualPath);
                sendLine(fd, "250 Directory changed.");
            } else {
                sendLine(fd, "550 Failed.");
            }
            continue;
        }
        if (verb == "CWD") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk()) {
                sendLine(fd, "550 Invalid directory.");
                continue;
            }
            if (!p.value().isRoot && !p.value().isMountRoot) {
                struct stat st{};
                if (stat(p.value().fsPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                    sendLine(fd, "550 Directory not found.");
                    continue;
                }
            }
            session.setCwd(p.value().virtualPath);
            sendLine(fd, "250 Directory changed.");
            continue;
        }
        if (verb == "PASV") {
            u16 pPort = 0;
            const u32 ip = net_gethostip();
            if (ip == 0 || !openPasv(pasvFd, pPort)) {
                sendLine(fd, "425 Cannot open passive connection.");
            } else {
                sendLine(fd, formatPasvReply(ip, pPort));
            }
            continue;
        }
        if (verb == "REST") {
            session.setRest(static_cast<u64>(std::strtoull(arg.c_str(), nullptr, 10)));
            sendLine(fd, "350 Restart position accepted.");
            continue;
        }
        if (verb == "SIZE") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            struct stat st{};
            if (!p.isOk() || p.value().isRoot || p.value().isMountRoot ||
                stat(p.value().fsPath.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
                sendLine(fd, "550 File not found.");
            } else {
                sendLine(fd, "213 " + std::to_string(static_cast<unsigned long long>(st.st_size)));
            }
            continue;
        }
        if (verb == "LIST" || verb == "NLST" || verb == "MLSD") {
            std::string_view listArg = arg;
            if (listArg.starts_with("-")) listArg = {};
            auto p = resolveFtpPath(session.cwd(), listArg, mounts_);
            if (!p.isOk()) {
                sendLine(fd, "550 Invalid path.");
                continue;
            }
            sendLine(fd, "150 Opening data connection.");
            s32 dataFd = acceptPasv(pasvFd);
            if (dataFd < 0) {
                sendLine(fd, "425 Data connection failed.");
                continue;
            }
            std::string listing;
            if (p.value().isRoot) {
                for (const auto& m : mounts_) {
                    if (verb == "NLST") listing += m.name + "\r\n";
                    else if (verb == "MLSD") listing += formatMlsdLine(m.name, true, 0);
                    else listing += formatUnixListLine(m.name, true, 0);
                }
            } else if (DIR* d = opendir(p.value().fsPath.c_str())) {
                while (dirent* ent = readdir(d)) {
                    std::string_view name(ent->d_name);
                    if (name == "." || name == "..") continue;
                    std::string full = p.value().fsPath;
                    if (!full.empty() && full.back() != '/') full += '/';
                    full.append(name.data(), name.size());
                    struct stat st{};
                    const bool okSt = stat(full.c_str(), &st) == 0;
                    const bool isDir = okSt && S_ISDIR(st.st_mode);
                    const u64 sz = okSt && !isDir ? static_cast<u64>(st.st_size) : 0;
                    if (verb == "NLST") listing += std::string(name) + "\r\n";
                    else if (verb == "MLSD") listing += formatMlsdLine(name, isDir, sz);
                    else listing += formatUnixListLine(name, isDir, sz);
                }
                closedir(d);
            }
            sendAll(dataFd, reinterpret_cast<const u8*>(listing.data()), listing.size(), stop_);
            net_close(dataFd);
            sendLine(fd, "226 Transfer complete.");
            continue;
        }
        if (verb == "RETR") {
            const u64 rest = session.consumeRest();
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || p.value().isRoot || p.value().isMountRoot) {
                sendLine(fd, "550 File not found.");
                continue;
            }
            FILE* f = std::fopen(p.value().fsPath.c_str(), "rb");
            if (!f) {
                sendLine(fd, "550 Cannot open file.");
                continue;
            }
            if (rest > 0) std::fseek(f, static_cast<long>(rest), SEEK_SET);
            sendLine(fd, "150 Opening data connection.");
            s32 dataFd = acceptPasv(pasvFd);
            if (dataFd < 0) {
                std::fclose(f);
                sendLine(fd, "425 Data connection failed.");
                continue;
            }
            u64 total = 0;
            bool ok = true;
            setTransferState(true, "RETR", p.value().virtualPath, 0);
            while (!stop_) {
                const std::size_t n = std::fread(g_ftpBuf, 1, sizeof g_ftpBuf, f);
                if (n == 0) break;
                if (!sendAll(dataFd, g_ftpBuf, n, stop_)) {
                    ok = false;
                    break;
                }
                total += n;
                setTransferState(true, "RETR", p.value().virtualPath, total);
            }
            setTransferState(false, "", "", 0);
            std::fclose(f);
            net_close(dataFd);
            sendLine(fd, ok ? "226 Transfer complete." : "426 Transfer aborted.");
            continue;
        }
        if (verb == "STOR" || verb == "APPE") {
            const u64 rest = session.consumeRest();
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || !ftpMutationAllowed(p.value())) {
                sendLine(fd, "550 Write not permitted.");
                continue;
            }
            const char* mode = (verb == "APPE") ? "ab" : (rest > 0 ? "r+b" : "wb");
            FILE* f = std::fopen(p.value().fsPath.c_str(), mode);
            if (!f && rest > 0) f = std::fopen(p.value().fsPath.c_str(), "wb");
            if (!f) {
                sendLine(fd, "550 Cannot open file for writing.");
                continue;
            }
            if (rest > 0 && verb == "STOR") std::fseek(f, static_cast<long>(rest), SEEK_SET);
            sendLine(fd, "150 Opening data connection.");
            s32 dataFd = acceptPasv(pasvFd);
            if (dataFd < 0) {
                std::fclose(f);
                sendLine(fd, "425 Data connection failed.");
                continue;
            }
            u64 total = 0;
            bool ok = true;
            setTransferState(true, verb, p.value().virtualPath, 0);
            while (!stop_) {
                pollsd dp{dataFd, kPollIn, 0};
                const s32 pr = net_poll(&dp, 1, kSliceMs);
                if (pr < 0) { ok = false; break; }
                if (pr == 0) continue;
                const s32 r = net_read(dataFd, g_ftpBuf, sizeof g_ftpBuf);
                if (r > 0) {
                    if (std::fwrite(g_ftpBuf, 1, static_cast<std::size_t>(r), f) != static_cast<std::size_t>(r)) {
                        ok = false;
                        break;
                    }
                    total += static_cast<u64>(r);
                    setTransferState(true, verb, p.value().virtualPath, total);
                } else if (r == 0) {
                    break;
                } else if (r != -EAGAIN && r != -EWOULDBLOCK) {
                    ok = false;
                    break;
                }
            }
            setTransferState(false, "", "", 0);
            std::fclose(f);
            net_close(dataFd);
            if (ok) {
                session.markDirty();
                LWP_MutexLock(mutex_);
                status_.dirty = true;
                LWP_MutexUnlock(mutex_);
                sendLine(fd, "226 Transfer complete.");
            } else {
                sendLine(fd, "451 Write error.");
            }
            continue;
        }
        if (verb == "DELE") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || !ftpMutationAllowed(p.value()) || std::remove(p.value().fsPath.c_str()) != 0) {
                sendLine(fd, "550 Delete failed.");
            } else {
                session.markDirty();
                LWP_MutexLock(mutex_);
                status_.dirty = true;
                LWP_MutexUnlock(mutex_);
                sendLine(fd, "250 File deleted.");
            }
            continue;
        }
        if (verb == "MKD" || verb == "XMKD") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || !ftpMutationAllowed(p.value()) || mkdir(p.value().fsPath.c_str(), 0755) != 0) {
                sendLine(fd, "550 Create directory failed.");
            } else {
                session.markDirty();
                LWP_MutexLock(mutex_);
                status_.dirty = true;
                LWP_MutexUnlock(mutex_);
                sendLine(fd, "257 \"" + p.value().virtualPath + "\" created.");
            }
            continue;
        }
        if (verb == "RMD" || verb == "XRMD") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || !ftpMutationAllowed(p.value()) || rmdir(p.value().fsPath.c_str()) != 0) {
                sendLine(fd, "550 Remove directory failed.");
            } else {
                session.markDirty();
                LWP_MutexLock(mutex_);
                status_.dirty = true;
                LWP_MutexUnlock(mutex_);
                sendLine(fd, "250 Directory removed.");
            }
            continue;
        }
        if (verb == "RNFR") {
            auto p = resolveFtpPath(session.cwd(), arg, mounts_);
            if (!p.isOk() || !ftpMutationAllowed(p.value())) {
                session.clearRenameFrom();
                sendLine(fd, "550 Rename not permitted.");
            } else {
                session.setRenameFrom(p.value().virtualPath);
                sendLine(fd, "350 Ready for RNTO.");
            }
            continue;
        }
        if (verb == "RNTO") {
            if (session.renameFrom().empty()) {
                sendLine(fd, "503 RNFR required first.");
                continue;
            }
            auto from = resolveFtpPath("/", session.renameFrom(), mounts_);
            auto to = resolveFtpPath(session.cwd(), arg, mounts_);
            session.clearRenameFrom();
            if (!from.isOk() || !to.isOk() || !ftpRenameAllowed(from.value(), to.value()) ||
                std::rename(from.value().fsPath.c_str(), to.value().fsPath.c_str()) != 0) {
                sendLine(fd, "550 Rename failed.");
            } else {
                session.markDirty();
                LWP_MutexLock(mutex_);
                status_.dirty = true;
                LWP_MutexUnlock(mutex_);
                sendLine(fd, "250 Rename successful.");
            }
            continue;
        }
        sendLine(fd, "502 Command not implemented.");
    }

    if (pasvFd >= 0) net_close(pasvFd);
    LWP_MutexLock(mutex_);
    status_.clientConnected = false;
    status_.transferring = false;
    status_.peerIp.clear();
    LWP_MutexUnlock(mutex_);
}

void FtpServer::loop() {
    s32 listenFd = -1;
    while (!stop_) {
        if (listenFd < 0) {
            const u32 ip = net_gethostip();
            const s32 fd = ip == 0 ? -1 : net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (fd >= 0) {
                const u32 one = 1;
                net_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port_);
                addr.sin_addr.s_addr = INADDR_ANY;
                if (net_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && net_listen(fd, 1) == 0) {
                    net_fcntl(fd, F_SETFL, kIosNonBlock);
                    listenFd = fd;
                    LWP_MutexLock(mutex_);
                    status_.listening = true;
                    status_.hostIp = ip;
                    status_.port = port_;
                    LWP_MutexUnlock(mutex_);
                    log("NL_FTP listen " + std::to_string(port_));
                } else {
                    net_close(fd);
                }
            }
            if (listenFd < 0) {
                for (int i = 0; i < 10 && !stop_; ++i) usleep(kSliceMs * 1000);
                continue;
            }
        }
        pollsd p{listenFd, kPollIn, 0};
        const s32 r = net_poll(&p, 1, kSliceMs);
        if (r < 0) {
            net_close(listenFd);
            listenFd = -1;
            continue;
        }
        if (r == 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        const s32 fd = net_accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
        if (fd < 0) continue;
        net_fcntl(fd, F_SETFL, kIosNonBlock);
        handleClient(fd, formatIpv4(peer.sin_addr.s_addr));
        net_close(fd);
    }
    if (listenFd >= 0) net_close(listenFd);
}

}  // namespace nl::platform
