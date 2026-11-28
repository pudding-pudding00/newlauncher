#include "core/wiiload_zip.hpp"

#include <algorithm>
#include <cctype>

#include "core/zip_reader.hpp"

namespace nl {
namespace {

bool isValidAppName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

std::string parentDir(std::string_view path) {
    const std::size_t pos = path.rfind('/');
    return pos == std::string_view::npos ? std::string{} : std::string(path.substr(0, pos));
}

void removeTree(FileSystem& fs, std::string_view dir) {
    auto listed = fs.list(dir);
    if (!listed.isOk()) return;
    for (const auto& e : listed.value()) {
        std::string child = joinPath(dir, e.name);
        if (e.isDir) {
            removeTree(fs, child);
        }
        (void)fs.removeFile(child);
    }
    (void)fs.removeFile(dir);
}

}  // namespace

Result<WiiloadZipPlan> inspectWiiloadZip(std::span<const u8> zipBytes) {
    using R = Result<WiiloadZipPlan>;
    auto dir = readZipDirectory(zipBytes);
    if (!dir.isOk()) {
        return R::err(dir.error());
    }
    const auto& entries = dir.value();
    if (entries.empty()) {
        return R::err({ErrorCode::InvalidData, "wiiload zip: empty archive"});
    }

    u64 totalUnzipped = 0;
    std::string bundleApp;
    bool hasAppsEntry = false;
    bool hasRootEntry = false;
    std::string dolEntry;
    std::string elfEntry;

    for (const auto& e : entries) {
        if (!isSafeZipPath(e.name)) {
            return R::err({ErrorCode::InvalidData, "wiiload zip: unsafe path " + e.name});
        }
        totalUnzipped += e.size;
        if (totalUnzipped > kWiiloadMaxZipUnzipped) {
            return R::err({ErrorCode::InvalidData, "wiiload zip: exceeds 32 MiB limit"});
        }
        if (e.isDir) continue;

        if (e.name.starts_with("apps/")) {
            hasAppsEntry = true;
            const std::string_view rest = std::string_view(e.name).substr(5);
            const std::size_t slash = rest.find('/');
            if (slash == std::string_view::npos) {
                return R::err({ErrorCode::InvalidData, "wiiload zip: file directly under apps/"});
            }
            const std::string_view app = rest.substr(0, slash);
            if (!isValidAppName(app)) {
                return R::err({ErrorCode::InvalidData, "wiiload zip: invalid app name"});
            }
            if (bundleApp.empty()) {
                bundleApp = std::string(app);
            } else if (bundleApp != app) {
                return R::err({ErrorCode::InvalidData, "wiiload zip: multiple apps in one bundle"});
            }
            const std::string_view sub = rest.substr(slash + 1);
            if (sub == "boot.dol") dolEntry = e.name;
            else if (sub == "boot.elf") elfEntry = e.name;
        } else {
            hasRootEntry = true;
            if (e.name == "boot.dol") dolEntry = e.name;
            else if (e.name == "boot.elf") elfEntry = e.name;
        }
    }

    if (hasAppsEntry && hasRootEntry) {
        return R::err({ErrorCode::InvalidData, "wiiload zip: mixed apps/ and root files"});
    }
    if (dolEntry.empty() && elfEntry.empty()) {
        return R::err({ErrorCode::InvalidData, "wiiload zip: missing boot.dol or boot.elf"});
    }

    WiiloadZipPlan plan;
    plan.isBundle = hasAppsEntry;
    plan.appName = bundleApp;
    plan.totalUnzipped = totalUnzipped;
    if (!dolEntry.empty()) {
        plan.entryPath = dolEntry;
        plan.entryKind = WiiloadKind::Dol;
    } else {
        plan.entryPath = elfEntry;
        plan.entryKind = WiiloadKind::Elf;
    }
    return R::ok(std::move(plan));
}

Result<std::vector<u8>> extractWiiloadZipEntry(std::span<const u8> zipBytes, std::string_view entryPath) {
    using R = Result<std::vector<u8>>;
    auto dir = readZipDirectory(zipBytes);
    if (!dir.isOk()) return R::err(dir.error());
    for (const auto& e : dir.value()) {
        if (!e.isDir && e.name == entryPath) {
            return extractZipEntry(zipBytes, e);
        }
    }
    return R::err({ErrorCode::NotFound, "wiiload zip: entry not found"});
}

Result<std::string> installWiiloadZip(FileSystem& fs, std::span<const u8> zipBytes,
                                      const WiiloadZipPlan& plan, std::string_view root) {
    using R = Result<std::string>;
    if (!plan.isBundle || !isValidAppName(plan.appName)) {
        return R::err({ErrorCode::InvalidData, "wiiload zip: not a valid bundle plan"});
    }
    auto dir = readZipDirectory(zipBytes);
    if (!dir.isOk()) return R::err(dir.error());

    const std::string appsDir = joinPath(root, "apps");
    const std::string finalDir = joinPath(appsDir, plan.appName);
    const std::string tmpDir = finalDir + ".tmp";
    const std::string oldDir = finalDir + ".old";

    removeTree(fs, tmpDir);
    removeTree(fs, oldDir);

    auto mk = fs.makeDirs(tmpDir);
    if (!mk.isOk()) return R::err(mk.error());

    const std::string prefix = "apps/" + plan.appName + "/";
    for (const auto& e : dir.value()) {
        if (e.isDir) continue;
        if (!e.name.starts_with(prefix)) continue;
        const std::string_view rel = std::string_view(e.name).substr(prefix.size());
        const std::string dst = joinPath(tmpDir, rel);
        const std::string pdir = parentDir(dst);
        if (!pdir.empty()) {
            auto dmk = fs.makeDirs(pdir);
            if (!dmk.isOk()) {
                removeTree(fs, tmpDir);
                return R::err(dmk.error());
            }
        }
        auto bytes = extractZipEntry(zipBytes, e);
        if (!bytes.isOk()) {
            removeTree(fs, tmpDir);
            return R::err(bytes.error());
        }
        auto wr = fs.writeAtomic(dst, bytes.value());
        if (!wr.isOk()) {
            removeTree(fs, tmpDir);
            return R::err(wr.error());
        }
    }

    const bool hadExisting = fs.list(finalDir).isOk();
    if (hadExisting) {
        auto mvOld = fs.renameFile(finalDir, oldDir);
        if (!mvOld.isOk()) {
            removeTree(fs, tmpDir);
            return R::err(mvOld.error());
        }
    }
    auto mvNew = fs.renameFile(tmpDir, finalDir);
    if (!mvNew.isOk()) {
        if (hadExisting) {
            (void)fs.renameFile(oldDir, finalDir);
        }
        removeTree(fs, tmpDir);
        return R::err(mvNew.error());
    }
    if (hadExisting) {
        removeTree(fs, oldDir);
    }

    const std::string_view entryRel = std::string_view(plan.entryPath).substr(prefix.size());
    return R::ok(joinPath(finalDir, entryRel));
}

}  // namespace nl
