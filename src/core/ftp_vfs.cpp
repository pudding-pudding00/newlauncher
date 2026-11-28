#include "core/ftp_vfs.hpp"

#include <vector>

namespace nl {
namespace {

bool hasForbiddenChars(std::string_view s) {
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '\\' || c == ':') return true;
    }
    return false;
}

}  // namespace

Result<FtpPath> resolveFtpPath(std::string_view cwd, std::string_view arg,
                               std::span<const FtpMount> mounts) {
    using R = Result<FtpPath>;
    if (hasForbiddenChars(cwd) || hasForbiddenChars(arg)) {
        return R::err({ErrorCode::InvalidData, "ftp: forbidden character in path"});
    }

    std::string combined;
    if (arg.empty()) {
        combined = cwd.empty() ? "/" : std::string(cwd);
    } else if (arg.front() == '/') {
        combined = std::string(arg);
    } else {
        combined = cwd.empty() ? "/" : std::string(cwd);
        if (combined.back() != '/') combined += '/';
        combined += arg;
    }

    std::vector<std::string_view> segs;
    std::size_t i = 0;
    while (i < combined.size()) {
        while (i < combined.size() && combined[i] == '/') ++i;
        std::size_t start = i;
        while (i < combined.size() && combined[i] != '/') ++i;
        if (i == start) break;
        std::string_view seg(combined.data() + start, i - start);
        if (seg == ".") continue;
        if (seg == "..") {
            if (!segs.empty()) segs.pop_back();
            continue;
        }
        segs.push_back(seg);
    }

    FtpPath out;
    if (segs.empty()) {
        out.isRoot = true;
        out.virtualPath = "/";
        return R::ok(std::move(out));
    }

    const std::string_view mountSeg = segs[0];
    if (mountSeg == "nand") {
        return R::err({ErrorCode::NotFound, "ftp: nand access forbidden"});
    }

    const FtpMount* found = nullptr;
    for (const auto& m : mounts) {
        if (m.name == mountSeg && m.name != "nand") {
            found = &m;
            break;
        }
    }
    if (found == nullptr) {
        return R::err({ErrorCode::NotFound, "ftp: unknown mount"});
    }

    out.isRoot = false;
    out.isMountRoot = (segs.size() == 1);
    out.mountName = found->name;
    out.writable = found->writable;

    out.virtualPath = "/" + found->name;
    out.fsPath = found->name + ":/";
    for (std::size_t k = 1; k < segs.size(); ++k) {
        out.virtualPath += '/';
        out.virtualPath.append(segs[k].data(), segs[k].size());
        if (k > 1) out.fsPath += '/';
        out.fsPath.append(segs[k].data(), segs[k].size());
    }
    return R::ok(std::move(out));
}

bool ftpMutationAllowed(const FtpPath& path) {
    return !path.isRoot && !path.isMountRoot && path.writable;
}

bool ftpRenameAllowed(const FtpPath& from, const FtpPath& to) {
    return ftpMutationAllowed(from) && ftpMutationAllowed(to) && from.mountName == to.mountName;
}

}  // namespace nl
