#include "core/emunand.hpp"

#include <algorithm>
#include <cstdio>

#include "core/launch_layout.hpp"
#include "core/library_merge.hpp"
#include "core/vc_map.hpp"

namespace nl {

namespace {

constexpr std::size_t kMaxTmdBytes = 64 * 1024;

bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::optional<u32> parseHex32(std::string_view s) {
    if (s.size() != 8) return std::nullopt;
    u32 v = 0;
    for (char c : s) {
        if (!isHexDigit(c)) return std::nullopt;
        const u32 d = (c >= '0' && c <= '9') ? static_cast<u32>(c - '0')
                    : (c >= 'a' && c <= 'f') ? static_cast<u32>(c - 'a' + 10)
                                             : static_cast<u32>(c - 'A' + 10);
        v = (v << 4) | d;
    }
    return v;
}

bool foldLess(std::string_view a, std::string_view b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        auto lower = [](unsigned char ch) -> unsigned char {
            return (ch >= 'A' && ch <= 'Z') ? static_cast<unsigned char>(ch - 'A' + 'a') : ch;
        };
        const unsigned char ca = lower(static_cast<unsigned char>(a[i]));
        const unsigned char cb = lower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

}  // namespace

std::optional<u64> parseTitleIdHex(std::string_view hex) {
    if (hex.size() != 16) return std::nullopt;
    const auto hi = parseHex32(hex.substr(0, 8));
    const auto lo = parseHex32(hex.substr(8, 8));
    if (!hi || !lo) return std::nullopt;
    return (static_cast<u64>(*hi) << 32) | *lo;
}

Result<EmuNandPath> parseEmuNandPath(std::string_view v) {
    auto err = []() { return Result<EmuNandPath>::err({ErrorCode::InvalidData, "emunand.path"}); };
    const std::size_t colon = v.find(":/");
    if (colon == std::string_view::npos || colon == 0 || colon > 8) return err();

    std::string dev;
    dev.reserve(colon);
    for (char c : v.substr(0, colon)) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return err();
        dev.push_back(c);
    }

    std::string_view folder = v.substr(colon + 1);
    while (folder.size() > 1 && folder.back() == '/') folder.remove_suffix(1);
    if (folder.size() <= 1 || folder.front() != '/' || folder.size() > kMaxEmuNandFolderLen) return err();

    std::size_t pos = 1;
    while (pos < folder.size()) {
        const std::size_t next = folder.find('/', pos);
        const std::string_view seg = folder.substr(pos, next == std::string_view::npos ? std::string_view::npos : next - pos);
        if (seg.empty() || seg == "." || seg == "..") return err();
        for (char c : seg) {
            if (c == '\\' || static_cast<unsigned char>(c) < 0x20) return err();
        }
        if (next == std::string_view::npos) break;
        pos = next + 1;
    }

    return Result<EmuNandPath>::ok(EmuNandPath{std::move(dev), std::string(folder)});
}

EmuNandInfo checkEmuNand(FileSystem& fs, std::string_view rawPath, std::span<const DeviceSlot> devices) {
    if (rawPath.empty()) return {EmuNandStatus::NotConfigured, {}, false, -1, {}};
    auto parsed = parseEmuNandPath(rawPath);
    if (!parsed.isOk()) return {EmuNandStatus::InvalidPath, {}, false, -1, std::string(rawPath)};

    const EmuNandPath& p = parsed.value();
    const std::string devLabel = deviceLabel(p.device);
    const DeviceSlot* slot = nullptr;
    for (const DeviceSlot& d : devices) {
        if (d.mount && d.name == p.device) {
            slot = &d;
            break;
        }
    }
    if (slot == nullptr) return {EmuNandStatus::DeviceMissing, p, false, -1, devLabel};
    if (slot->kind != FsKind::Fat) return {EmuNandStatus::NotFat, p, slot->usb, slot->mbrIndex, devLabel};
    if (slot->usb && slot->mbrIndex < 0) return {EmuNandStatus::NotPrimaryMbr, p, true, -1, devLabel};

    const std::string titleDir = p.device + ":" + p.folder + "/title";
    if (!fs.list(titleDir).isOk()) return {EmuNandStatus::MissingTitleDir, p, slot->usb, slot->mbrIndex, std::string(rawPath)};
    return {EmuNandStatus::Ok, p, slot->usb, slot->mbrIndex, devLabel};
}

std::vector<EmuTitle> scanEmuNand(FileSystem& fs, const EmuNandPath& p, BannerLang lang, u32* skipped) {
    u32 dummySkipped = 0;
    u32& skipCount = skipped != nullptr ? *skipped : dummySkipped;
    skipCount = 0;

    const std::string baseDir = p.device + ":" + p.folder + "/title/00010001";
    auto entries = fs.list(baseDir);
    if (!entries.isOk()) return {};

    std::vector<EmuTitle> out;
    for (const DirEntry& e : entries.value()) {
        if (!e.isDir) continue;
        const auto low = parseHex32(e.name);
        if (!low) continue;
        if (out.size() >= kMaxEmuTitles) {
            ++skipCount;
            continue;
        }

        const std::string contentDir = baseDir + "/" + e.name + "/content";
        const auto tmdBytes = fs.readAll(contentDir + "/title.tmd", kMaxTmdBytes);
        if (!tmdBytes.isOk()) {
            ++skipCount;
            continue;
        }
        const auto tmd = parseTmd(tmdBytes.value());
        if (!tmd.isOk()) {
            ++skipCount;
            continue;
        }
        const auto cid = contentIdForIndex(tmd.value(), 0);
        if (!cid) {
            ++skipCount;
            continue;
        }

        char hexId[16];
        std::snprintf(hexId, sizeof hexId, "%08x.app", static_cast<unsigned>(*cid));
        const std::string appPath = contentDir + "/" + hexId;
        std::vector<u8> head(kImetReadBytes, 0);
        const auto rd = fs.read(appPath, 0, head);
        if (!rd.isOk() || rd.value() == 0) {
            ++skipCount;
            continue;
        }

        const u64 tid = (u64{kChannelTitleType} << 32) | *low;
        std::string name = imetChannelName(std::span<const u8>(head.data(), rd.value()), lang);
        if (name.empty()) name = formatTitleId(tid);
        out.push_back(EmuTitle{tid, std::move(name), *cid, appPath});
    }

    std::sort(out.begin(), out.end(), [](const EmuTitle& a, const EmuTitle& b) {
        if (foldLess(a.name, b.name)) return true;
        if (foldLess(b.name, a.name)) return false;
        return a.titleId < b.titleId;
    });
    return out;
}

std::string emuNandStatusText(const EmuNandInfo& info, u32 titleCount, bool de) {
    switch (info.status) {
        case EmuNandStatus::NotConfigured: return de ? "nicht eingerichtet" : "not configured";
        case EmuNandStatus::InvalidPath: return (de ? "ungültiger Pfad: " : "invalid path: ") + info.detail;
        case EmuNandStatus::DeviceMissing: return (de ? "Gerät fehlt: " : "device missing: ") + info.detail;
        case EmuNandStatus::NotFat: return (de ? "kein FAT32: " : "not FAT32: ") + info.detail;
        case EmuNandStatus::NotPrimaryMbr:
            return (de ? "keine primäre MBR-Partition: " : "not a primary MBR partition: ") + info.detail;
        case EmuNandStatus::MissingTitleDir:
            return (de ? "Ordner fehlt: " : "missing folder: ") + info.detail + "/title";
        case EmuNandStatus::Ok:
            return info.path.device + ":" + info.path.folder + " (" + std::to_string(titleCount) +
                   (de ? " Kanäle)" : " channels)");
    }
    return {};
}

}  // namespace nl
