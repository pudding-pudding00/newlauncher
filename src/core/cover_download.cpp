#include "core/cover_download.hpp"

#include <algorithm>

#include "core/cover_paths.hpp"
#include <string>

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

std::unordered_set<std::string> existingCoverFiles(FileSystem& fs, std::string_view libRoot) {
    std::unordered_set<std::string> out;
    const std::string root(libRoot);
    for (const char* base : {"/newlauncher/covers_src/", "/newlauncher/cache/covers/"}) {
        for (const char* console : {"wii", "gc"}) {
            const std::string dir = root + base + console;
            auto entries = fs.list(dir);
            if (!entries.isOk()) continue;  // Ordner fehlt: noch kein Cover
            for (const DirEntry& e : entries.value())
                if (!e.isDir) out.insert(dir + "/" + e.name);
        }
    }
    return out;
}

DownloadSummary runCoverDownloads(std::span<const CoverJob> jobs, std::span<const std::string> regions,
                                  std::string_view baseUrl, const CoverDownloadIo& io) {
    DownloadSummary sum;
    u32 streak = 0;  // Fehler in Folge
    std::vector<u8> body;
    for (const CoverJob& job : jobs) {
        enum class Res { Missing, Ok, Failed } res = Res::Missing;
        std::string_view okRegion;
        if (streak >= kMaxConsecutiveDownloadFailures || io.cancelled()) {
            res = Res::Failed;  // nicht mehr versucht
        } else {
            for (const std::string& region : regions) {
                if (io.cancelled()) {
                    res = Res::Failed;
                    break;
                }
                const auto url = parseUrl(gametdbCoverUrl(baseUrl, region, job.id6));
                if (!url) {
                    res = Res::Failed;
                    break;
                }
                const HttpOutcome o = io.get(*url, body);
                if (o == HttpOutcome::Failed) {
                    res = Res::Failed;
                    break;
                }
                if (o == HttpOutcome::NotFound || !isPng(body)) continue;
                if (!io.save(job.stem, body)) {
                    res = Res::Failed;
                    break;
                }
                res = Res::Ok;
                okRegion = region;
                break;
            }
            if (res == Res::Failed) {
                if (++streak == kMaxConsecutiveDownloadFailures)
                    io.log("NL_COVER_DL_ABORT failures=" + std::to_string(kMaxConsecutiveDownloadFailures));
            } else {
                streak = 0;
            }
        }
        switch (res) {
            case Res::Ok:
                ++sum.ok;
                io.finished(job.stem);
                io.log("NL_COVER_DL " + job.id6 + " " + std::string(okRegion) + " ok");
                break;
            case Res::Missing:
                ++sum.missing;
                io.log("NL_COVER_DL " + job.id6 + " missing");
                break;
            case Res::Failed:
                ++sum.failed;
                io.log("NL_COVER_DL " + job.id6 + " failed");
                break;
        }
        io.jobDone();
    }
    return sum;
}

bool isPng(std::span<const u8> data) {
    static constexpr u8 kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return data.size() >= 8 && std::equal(kSig, kSig + 8, data.begin());
}

}  // namespace nl
