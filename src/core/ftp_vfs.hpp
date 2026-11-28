#pragma once
// Virtual filesystem path sanitizer and mount jail for the passive FTP server (M32).
// Strictly exposes only explicitly mounted SD/USB drives; nand:/ is never accessible.

#include <span>
#include <string>
#include <string_view>

#include "core/result.hpp"

namespace nl {

struct FtpMount {
    std::string name;     // e.g. "sd", "usb", "usb2"
    bool writable = false;
};

struct FtpPath {
    bool isRoot = false;
    bool isMountRoot = false;
    std::string mountName;
    bool writable = false;
    std::string virtualPath;  // e.g. "/", "/sd", "/sd/apps/demo"
    std::string fsPath;       // e.g. "sd:/", "sd:/apps/demo" (empty for isRoot)
};

Result<FtpPath> resolveFtpPath(std::string_view cwd, std::string_view arg,
                               std::span<const FtpMount> mounts);

bool ftpMutationAllowed(const FtpPath& path);
bool ftpRenameAllowed(const FtpPath& from, const FtpPath& to);

}  // namespace nl
