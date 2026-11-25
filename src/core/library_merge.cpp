#include "core/library_merge.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "core/text_fold.hpp"
#include "core/wbfs_part.hpp"

namespace nl {

MergedLibrary mergeIndexes(std::span<const DeviceIndex> inOrder) {
    MergedLibrary lib;
    std::unordered_map<std::string, std::size_t> byKey;
    for (std::size_t d = 0; d < inOrder.size(); ++d) {
        lib.devices.push_back(inOrder[d].device);
        for (const IndexGame& g : inOrder[d].index.games) {
            auto [it, fresh] = byKey.emplace(g.key, lib.games.size());
            if (!fresh) {
                std::vector<u8>& also = lib.games[it->second].alsoOn;
                if (std::find(also.begin(), also.end(), static_cast<u8>(d)) == also.end()) also.push_back(static_cast<u8>(d));
                continue;
            }
            lib.games.push_back(MergedGame{g, static_cast<u8>(d), {}});
        }
    }
    std::stable_sort(lib.games.begin(), lib.games.end(), [](const MergedGame& a, const MergedGame& b) {
        if (a.game.console != b.game.console) return a.game.console < b.game.console;
        return compareTitles(a.game.title, b.game.title) < 0;
    });
    return lib;
}

std::string deviceLabel(std::string_view device) {
    if (device == "sd") return "SD";
    if (device == "usb") return "USB 1";
    if (device.size() == 4 && device.substr(0, 3) == "usb") return "USB " + std::string(1, device[3]);
    std::string out(device);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::string coverRoot(std::string_view device, bool writable, std::string_view dataDir) {
    if (writable) return std::string(device) + ":";
    return std::string(dataDir) + "/cache/" + std::string(device);
}

std::vector<std::string> coverRootsFor(std::span<const std::string> devices, std::span<const bool> writable,
                                       std::string_view dataDir, CoverStorage storage, bool sdWritable) {
    std::vector<std::string> roots;
    roots.reserve(devices.size());
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (storage == CoverStorage::Sd && sdWritable) roots.emplace_back("sd:");
        else roots.push_back(coverRoot(devices[i], i < writable.size() && writable[i], dataDir));
    }
    return roots;
}

std::vector<std::string> uniqueRoots(std::span<const std::string> roots) {
    std::vector<std::string> out;
    for (const std::string& r : roots)
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    return out;
}

std::string deviceCacheDir(std::string_view device, bool writable, std::string_view dataDir) {
    return coverRoot(device, writable, dataDir) + "/newlauncher/cache";
}

Result<MultiScan> scanDevices(FileSystem& fs, std::span<const std::string> devices, const ScanRules& rules,
                              std::span<const DeviceIndex> previous, const MetaDb* meta, ScanProgress progress,
                              void* user, std::span<const RawSource> raw) {
    MultiScan out;
    for (const std::string& device : devices) {
        const IndexData* prev = nullptr;
        for (const DeviceIndex& p : previous)
            if (p.device == device) prev = &p.index;
        const RawSource* src = nullptr;
        for (const RawSource& r : raw)
            if (r.device == device && r.dev != nullptr) src = &r;
        if (src != nullptr) {
            // WBFS-Partition (M16): kein Dateisystem, die Slots werden bei jedem Scan roh gelesen.
            if (progress != nullptr && !progress(user, 0)) return Result<MultiScan>::err(Error{ErrorCode::Cancelled, device});
            auto w = scanWbfsPartition(*src->dev, src->partLba, meta);
            if (!w.isOk()) continue;  // kein gültiger WBFS-Kopf
            WbfsScan ws = std::move(w).value();
            std::set<std::string_view> before;
            if (prev != nullptr)
                for (const IndexGame& g : prev->games) before.insert(g.key);
            std::set<std::string_view> after;
            for (const IndexGame& g : ws.games) {
                after.insert(g.key);
                if (!before.count(g.key)) ++out.stats.added;
            }
            for (std::string_view k : before) out.stats.removed += after.count(k) ? 0 : 1;
            out.stats.headersRead += static_cast<u32>(ws.games.size() + ws.skippedSlots.size());
            out.stats.skipped += static_cast<u32>(ws.skippedSlots.size());
            for (u32 slot : ws.skippedSlots) out.wbfsSkips.emplace_back(device, slot);
            IndexData idx;
            idx.games = std::move(ws.games);
            out.indexes.push_back(DeviceIndex{device, std::move(idx)});
            continue;
        }
        auto r = scanLibrary(fs, device + ":", rules, prev, meta, progress, user);
        if (!r.isOk()) {
            if (r.error().code == ErrorCode::Cancelled) return Result<MultiScan>::err(r.error());
            continue;  // nicht eingehängt oder nicht lesbar
        }
        ScanResult s = std::move(r).value();
        out.stats.added += s.stats.added;
        out.stats.removed += s.stats.removed;
        out.stats.headersRead += s.stats.headersRead;
        out.stats.skipped += s.stats.skipped;
        out.indexes.push_back(DeviceIndex{device, std::move(s.index)});
    }
    if (out.indexes.empty()) return Result<MultiScan>::err(Error{ErrorCode::NotMounted, "kein Bibliotheksgerät lesbar"});
    return Result<MultiScan>::ok(std::move(out));
}

}  // namespace nl
