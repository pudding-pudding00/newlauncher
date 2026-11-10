#include "core/cover_download.hpp"

#include <algorithm>

#include "core/cover_paths.hpp"

namespace nl {
namespace {
constexpr std::string_view kRegions[] = {"EN", "DE", "FR", "US", "JA"};

bool knownRegion(std::string_view r) { return std::find(std::begin(kRegions), std::end(kRegions), r) != std::end(kRegions); }
}  // namespace

std::vector<std::string_view> coverRegions(std::string_view configRegion, std::string_view systemRegion) {
    std::vector<std::string_view> out;
    auto add = [&](std::string_view r) {
        if (knownRegion(r) && std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    };
    add(knownRegion(configRegion) ? configRegion : systemRegion);
    for (std::string_view r : {"EN", "US", "JA"}) add(r);
    return out;
}

std::string gametdbCoverUrl(std::string_view base, std::string_view region, std::string_view id6) {
    std::string url(base);
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += '/';
    url += region;
    url += '/';
    url += id6;
    url += ".png";
    return url;
}

std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games,
                                        const std::function<bool(const std::string&)>& exists,
                                        std::string_view libRoot) {
    std::vector<CoverJob> jobs;
    for (const Game* g : games) {
        if (!g || !g->key || (g->console != Console::Wii && g->console != Console::Gc)) continue;
        std::string stem = coverStem(*g);
        if (exists(coverSrcPath(libRoot, stem)) || exists(coverTexPath(libRoot, stem))) continue;
        jobs.push_back({std::string(g->key), std::move(stem)});
    }
    return jobs;
}

bool isPng(std::span<const u8> data) {
    static constexpr u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return data.size() >= 8 && std::equal(kSig, kSig + 8, data.begin());
}

}  // namespace nl
