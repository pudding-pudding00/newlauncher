#include "core/library_merge.hpp"

#include <algorithm>
#include <unordered_map>

#include "core/text_fold.hpp"

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

std::string deviceCacheDir(std::string_view device, bool writable, std::string_view dataDir) {
    return coverRoot(device, writable, dataDir) + "/newlauncher/cache";
}

Result<MultiScan> scanDevices(FileSystem& fs, std::span<const std::string> devices, const ScanRules& rules,
                              std::span<const DeviceIndex> previous, const MetaDb* meta, ScanProgress progress,
                              void* user) {
    MultiScan out;
    for (const std::string& device : devices) {
        const IndexData* prev = nullptr;
        for (const DeviceIndex& p : previous)
            if (p.device == device) prev = &p.index;
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
