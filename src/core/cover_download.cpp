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

namespace {
// libretro-Systemname (libretro-database), nullptr für Konsolen ohne Boxarts dort.
const char* libretroSystem(Console c) {
    switch (c) {
        case Console::Nes: return "Nintendo - Nintendo Entertainment System";
        case Console::Snes: return "Nintendo - Super Nintendo Entertainment System";
        case Console::Gb: return "Nintendo - Game Boy";
        case Console::Gbc: return "Nintendo - Game Boy Color";
        case Console::N64: return "Nintendo - Nintendo 64";
        case Console::Gba: return "Nintendo - Game Boy Advance";
        case Console::Gc:
        case Console::Wii:
        case Console::Channel: return nullptr;
    }
    return nullptr;
}

void percentEncode(std::string& out, std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '.' || c == '_' || c == '~';
        if (plain) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
}
}  // namespace

std::string libretroCoverUrl(std::string_view base, Console console, std::string_view fileStem) {
    const char* system = libretroSystem(console);
    if (system == nullptr || base.empty()) return {};
    std::string url(base);
    if (url.back() == '/') url.pop_back();
    url += '/';
    for (const char* p = system; *p; ++p) url += *p == ' ' ? '_' : *p;
    url += "/master/Named_Boxarts/";
    std::string name(fileStem);
    for (char& ch : name)
        if (std::string_view("&*/:`<>?\\|\"").find(ch) != std::string_view::npos) ch = '_';
    percentEncode(url, name);
    url += ".png";
    return url;
}

std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games,
                                        const std::function<bool(const std::string&)>& exists,
                                        std::string_view libRoot, std::string_view retroBase,
                                        const std::unordered_set<std::string>* skip) {
    std::vector<CoverJob> jobs;
    for (const Game* g : games) {
        if (!g || !g->key || g->console == Console::Channel) continue;
        const bool disc = g->console == Console::Wii || g->console == Console::Gc;
        if (!disc && retroBase.empty()) continue;
        std::string stem = coverStem(*g);
        if (skip != nullptr && skip->count(stem) > 0) continue;
        if (exists(coverSrcPath(libRoot, stem)) || exists(coverTexPath(libRoot, stem))) continue;
        if (disc) {
            jobs.push_back({std::string(g->key), std::move(stem), std::string(libRoot), {}});
            continue;
        }
        const std::string_view fileStem = std::string_view(stem).substr(stem.find('/') + 1);
        std::string url = libretroCoverUrl(retroBase, g->console, fileStem);
        if (url.empty()) continue;
        jobs.push_back({stem, stem, std::string(libRoot), std::move(url)});
    }
    return jobs;
}

std::string missingListPath(std::string_view libRoot) {
    return std::string(libRoot) + "/newlauncher/cache/covers/missing.txt";
}

std::vector<std::string> parseMissingList(std::string_view text) {
    std::vector<std::string> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        if (line.empty() || std::find(out.begin(), out.end(), line) != out.end()) continue;
        out.emplace_back(line);
    }
    return out;
}

std::vector<std::string> updateMissingList(const std::vector<std::string>& old, const std::vector<std::string>& jobStems,
                                           const std::vector<std::string>& missed) {
    std::vector<std::string> out;
    for (const std::string& s : old)
        if (std::find(jobStems.begin(), jobStems.end(), s) == jobStems.end()) out.push_back(s);
    out.insert(out.end(), missed.begin(), missed.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string missingListText(const std::vector<std::string>& stems) {
    std::string out;
    for (const std::string& s : stems) {
        out += s;
        out += '\n';
    }
    return out;
}

std::unordered_set<std::string> existingCoverFiles(FileSystem& fs, std::string_view libRoot, bool retro) {
    std::unordered_set<std::string> out;
    const std::string root(libRoot);
    std::vector<const char*> consoles = {"wii", "gc"};
    if (retro) {
        consoles.clear();
        for (int i = 0; i < kConsoleCount; ++i) {
            const ConsoleInfo& info = consoleInfo(static_cast<Console>(i));
            if (info.id != Console::Channel) consoles.push_back(info.key);
        }
    }
    for (const char* base : {"/newlauncher/covers_src/", "/newlauncher/cache/covers/"}) {
        for (const char* console : consoles) {
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
        static const std::string kLibretro[] = {"libretro"};
        const std::span<const std::string> tries = job.url.empty() ? regions : std::span<const std::string>(kLibretro);
        if (streak >= kMaxConsecutiveDownloadFailures || io.cancelled()) {
            res = Res::Failed;  // nicht mehr versucht
        } else {
            for (const std::string& region : tries) {
                if (io.cancelled()) {
                    res = Res::Failed;
                    break;
                }
                const auto url = parseUrl(job.url.empty() ? gametdbCoverUrl(baseUrl, region, job.id6) : job.url);
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
                if (io.missed) io.missed(job.stem);
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

bool shouldAutoFetchCovers(const CoverAutoInputs& in) {
    return in.enabled && in.ready && !in.scanRunning && !in.netBusy && !in.alreadyRan;
}

}  // namespace nl
