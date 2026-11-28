#pragma once
// RFC 959 passive FTP command parser, session state machine, and formatting helpers (M32).

#include <optional>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

inline constexpr u16 kFtpDefaultPort = 21;
inline constexpr int kFtpMaxAuthFailures = 5;
inline constexpr u64 kFtpLockoutMs = 30000;

struct FtpCommand {
    std::string verb;  // uppercased ASCII verb
    std::string arg;   // raw argument (without trailing CR/LF)
};

std::optional<FtpCommand> parseFtpCommand(std::string_view line);

std::string formatPasvReply(u32 ipHostOrder, u16 port);
std::string formatUnixListLine(std::string_view name, bool isDir, u64 size);
std::string formatMlsdLine(std::string_view name, bool isDir, u64 size);
std::string formatFtpUri(std::string_view user, std::string_view pass, u32 ipHostOrder, u16 port);

enum class FtpAuthResult : u8 { Ok, BadCredentials, LockedOut };

class FtpSession {
public:
    void setCredentials(std::string user, std::string pass);
    void reset();

    void setPendingUser(std::string user) { pendingUser_ = std::move(user); }
    FtpAuthResult authenticate(std::string_view pass, u64 nowMs);
    bool loggedIn() const { return loggedIn_; }
    bool isLockedOut(u64 nowMs) const { return nowMs < lockoutUntilMs_; }

    const std::string& cwd() const { return cwd_; }
    void setCwd(std::string cwd) { cwd_ = std::move(cwd); }
    std::string formatPwdReply() const;

    void setRest(u64 offset) { restOffset_ = offset; }
    u64 consumeRest() {
        const u64 v = restOffset_;
        restOffset_ = 0;
        return v;
    }

    void setRenameFrom(std::string path) { renameFrom_ = std::move(path); }
    const std::string& renameFrom() const { return renameFrom_; }
    void clearRenameFrom() { renameFrom_.clear(); }

    void markDirty() { dirty_ = true; }
    bool dirty() const { return dirty_; }

private:
    std::string expectedUser_ = "wii";
    std::string expectedPass_;
    std::string pendingUser_;
    bool loggedIn_ = false;
    int failCount_ = 0;
    u64 lockoutUntilMs_ = 0;
    std::string cwd_ = "/";
    u64 restOffset_ = 0;
    std::string renameFrom_;
    bool dirty_ = false;
};

}  // namespace nl
