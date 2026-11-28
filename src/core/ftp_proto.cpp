#include "core/ftp_proto.hpp"

#include <cctype>
#include <cstdio>

namespace nl {
namespace {

bool constantTimeEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    u8 diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<u8>(a[i] ^ b[i]);
    }
    return diff == 0;
}

}  // namespace

std::optional<FtpCommand> parseFtpCommand(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    while (!line.empty() && line.front() == ' ') {
        line.remove_prefix(1);
    }
    if (line.empty()) return std::nullopt;

    std::size_t sp = line.find(' ');
    std::string_view verbView = sp == std::string_view::npos ? line : line.substr(0, sp);
    if (verbView.empty() || verbView.size() > 16) return std::nullopt;

    FtpCommand cmd;
    cmd.verb.reserve(verbView.size());
    for (char c : verbView) {
        if (!std::isalpha(static_cast<unsigned char>(c))) return std::nullopt;
        cmd.verb.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (sp != std::string_view::npos) {
        cmd.arg = std::string(line.substr(sp + 1));
    }
    return cmd;
}

std::string formatPasvReply(u32 ipHostOrder, u16 port) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).",
                  (ipHostOrder >> 24) & 0xFFu, (ipHostOrder >> 16) & 0xFFu,
                  (ipHostOrder >> 8) & 0xFFu, ipHostOrder & 0xFFu,
                  static_cast<unsigned>(port >> 8), static_cast<unsigned>(port & 0xFFu));
    return buf;
}

std::string formatUnixListLine(std::string_view name, bool isDir, u64 size) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s 1 wii wii %12llu Jan  1  2026 ",
                  isDir ? "drwxr-xr-x" : "-rw-r--r--",
                  static_cast<unsigned long long>(size));
    std::string out(buf);
    out.append(name.data(), name.size());
    out += "\r\n";
    return out;
}

std::string formatMlsdLine(std::string_view name, bool isDir, u64 size) {
    char buf[96];
    if (isDir) {
        std::snprintf(buf, sizeof buf, "type=dir; ");
    } else {
        std::snprintf(buf, sizeof buf, "type=file;size=%llu; ", static_cast<unsigned long long>(size));
    }
    std::string out(buf);
    out.append(name.data(), name.size());
    out += "\r\n";
    return out;
}

std::string formatFtpUri(std::string_view user, std::string_view pass, u32 ipHostOrder, u16 port) {
    char ipBuf[32];
    std::snprintf(ipBuf, sizeof ipBuf, "%u.%u.%u.%u",
                  (ipHostOrder >> 24) & 0xFFu, (ipHostOrder >> 16) & 0xFFu,
                  (ipHostOrder >> 8) & 0xFFu, ipHostOrder & 0xFFu);
    std::string out = "ftp://";
    if (!user.empty()) {
        out.append(user.data(), user.size());
        if (!pass.empty()) {
            out += ':';
            out.append(pass.data(), pass.size());
        }
        out += '@';
    }
    out += ipBuf;
    if (port != kFtpDefaultPort) {
        out += ':';
        out += std::to_string(port);
    }
    out += '/';
    return out;
}

void FtpSession::setCredentials(std::string user, std::string pass) {
    expectedUser_ = std::move(user);
    expectedPass_ = std::move(pass);
}

void FtpSession::reset() {
    pendingUser_.clear();
    loggedIn_ = false;
    cwd_ = "/";
    restOffset_ = 0;
    renameFrom_.clear();
}

FtpAuthResult FtpSession::authenticate(std::string_view pass, u64 nowMs) {
    if (nowMs < lockoutUntilMs_) {
        return FtpAuthResult::LockedOut;
    }
    const bool userOk = constantTimeEquals(pendingUser_, expectedUser_);
    const bool passOk = constantTimeEquals(pass, expectedPass_);
    if (userOk && passOk) {
        loggedIn_ = true;
        failCount_ = 0;
        return FtpAuthResult::Ok;
    }
    ++failCount_;
    if (failCount_ >= kFtpMaxAuthFailures) {
        failCount_ = 0;
        lockoutUntilMs_ = nowMs + kFtpLockoutMs;
        return FtpAuthResult::LockedOut;
    }
    return FtpAuthResult::BadCredentials;
}

std::string FtpSession::formatPwdReply() const {
    std::string escaped;
    escaped.reserve(cwd_.size() + 4);
    for (char c : cwd_) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    return "257 \"" + escaped + "\" is current directory.";
}

}  // namespace nl
