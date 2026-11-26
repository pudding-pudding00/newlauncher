#include "core/cover_download.hpp"

#include <algorithm>

#include "core/cover_paths.hpp"
#include "core/url_encode.hpp"
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
    appendPercentEncoded(url, name);
    url += ".png";
    return url;
}

namespace {
constexpr std::string_view kNoIntroRegions[] = {
    "USA",     "Europe", "Japan",     "World",       "Asia",     "Australia", "Brazil",  "Canada",
    "China",   "France", "Germany",   "Hong Kong",   "Italy",    "Korea",     "Netherlands", "Spain",
    "Sweden",  "Taiwan", "UK",        "Russia",      "Scandinavia", "Latin America", "Mexico", "Portugal",
    "Denmark", "Norway", "Finland",   "Greece",      "Poland",   "Unknown"};

// Teilt s an sep; ruft f für jedes Stück, false sobald f false liefert.
template <class F>
bool allParts(std::string_view s, std::string_view sep, F f) {
    for (;;) {
        const std::size_t p = s.find(sep);
        if (!f(s.substr(0, p))) return false;
        if (p == std::string_view::npos) return true;
        s.remove_prefix(p + sep.size());
    }
}

bool isRegionTag(std::string_view t) {
    return allParts(t, ", ", [](std::string_view r) {
        return std::find(std::begin(kNoIntroRegions), std::end(kNoIntroRegions), r) != std::end(kNoIntroRegions);
    });
}

bool upper(char c) { return c >= 'A' && c <= 'Z'; }
bool lower(char c) { return c >= 'a' && c <= 'z'; }

// „En“, „En,Fr,De“, „Zh-Hant“
bool isLangTag(std::string_view t) {
    return allParts(t, ",", [](std::string_view l) {
        if (l.size() < 2 || !upper(l[0]) || !lower(l[1])) return false;
        l.remove_prefix(2);
        if (l.empty()) return true;
        if (l.size() < 3 || l[0] != '-' || !upper(l[1])) return false;
        return std::all_of(l.begin() + 2, l.end(), lower);
    });
}

bool isRevTag(std::string_view t) {
    if (t.size() < 5 || t.substr(0, 4) != "Rev ") return false;
    return std::all_of(t.begin() + 4, t.end(), [](char c) { return upper(c) || (c >= '0' && c <= '9') || c == '.'; });
}

using Tags = std::vector<std::string_view>;

// No-Intro-Name → Titel und die Klammer-Tags am Ende („Titel (A) (B)“).
std::string_view splitTags(std::string_view s, Tags& tags) {
    while (s.size() >= 4 && s.back() == ')') {
        const std::size_t open = s.rfind('(');
        if (open == std::string_view::npos || open == 0 || s[open - 1] != ' ') break;
        const std::string_view inner = s.substr(open + 1, s.size() - open - 2);
        if (inner.find(')') != std::string_view::npos) break;
        tags.insert(tags.begin(), inner);
        s = s.substr(0, open - 1);
    }
    return s;
}

std::string withTags(std::string_view title, std::initializer_list<const Tags*> groups) {
    std::string out(title);
    for (const Tags* g : groups)
        for (std::string_view t : *g) {
            out += " (";
            out += t;
            out += ')';
        }
    return out;
}
}  // namespace

std::vector<std::string> libretroNameCandidates(std::string_view stem) {
    std::vector<std::string> out;
    const auto add = [&](std::string name) {
        if (out.size() < kMaxLibretroCandidates && std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(std::move(name));
    };
    add(std::string(stem));
    Tags tags;
    const std::string_view title = splitTags(stem, tags);
    Tags reg, lang, rev, other;
    for (std::string_view t : tags) {
        if (isRegionTag(t)) reg.push_back(t);
        else if (isLangTag(t)) lang.push_back(t);
        else if (isRevTag(t)) rev.push_back(t);
        else other.push_back(t);
    }
    std::vector<std::string_view> titles = {title};
    if (const std::size_t tilde = title.find(" ~ "); tilde != std::string_view::npos)
        titles.push_back(title.substr(0, tilde));
    const Tags none, en = {"En"};
    // Sprachen: vorhandene, dann ohne; ohne Sprach-Tag zusätzlich „(En)“ (libretro ergänzt es oft).
    const std::vector<const Tags*> langs = lang.empty() ? std::vector<const Tags*>{&none, &en}
                                                        : std::vector<const Tags*>{&lang, &none};
    const std::vector<const Tags*> revs = rev.empty() ? std::vector<const Tags*>{&none}
                                                      : std::vector<const Tags*>{&rev, &none};
    std::vector<Tags> others = {other};
    if (other.size() > 1)
        for (std::string_view o : other) others.push_back({o});
    if (!other.empty()) others.push_back({});
    for (std::string_view ti : titles)
        for (const Tags* rv : revs)
            for (const Tags& ot : others)
                for (const Tags* la : langs) add(withTags(ti, {&reg, la, rv, &ot}));
    bool japanese = false;
    for (std::string_view r : reg) japanese = japanese || r.find("Japan") != std::string_view::npos;
    for (std::string_view ti : titles) {
        if (!reg.empty() && reg[0].find(", ") != std::string_view::npos)
            allParts(reg[0], ", ", [&](std::string_view r) {
                const Tags one = {r};
                for (const Tags* la : langs) add(withTags(ti, {&one, la}));
                return true;
            });
        for (std::string_view r : {"USA", "World", "USA, Europe", "Europe", "Japan, USA", "Japan"}) {
            if (r == "Japan" && !japanese) continue;  // kein japanisches Cover für Westausgaben
            const Tags one = {r};
            add(withTags(ti, {&one, &none}));
            add(withTags(ti, {&one, &en}));
        }
    }
    return out;
}

std::string libretroSymlinkTarget(std::span<const u8> body) {
    if (body.size() >= 512) return {};
    std::string_view t(reinterpret_cast<const char*>(body.data()), body.size());
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.remove_suffix(1);
    if (t.size() <= 4 || t.substr(t.size() - 4) != ".png") return {};
    for (const char ch : t) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F || ch == '/' || ch == '\\') return {};
    }
    return std::string(t);
}

std::string libretroLinkUrl(std::string_view url, std::string_view target) {
    std::string out(url.substr(0, url.rfind('/') + 1));
    appendPercentEncoded(out, target);
    return out;
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
            jobs.push_back({std::string(g->key), std::move(stem), std::string(libRoot), {}, g->console});
            continue;
        }
        const std::string_view fileStem = std::string_view(stem).substr(stem.find('/') + 1);
        std::vector<std::string> urls;
        for (const std::string& name : libretroNameCandidates(fileStem)) {
            std::string url = libretroCoverUrl(retroBase, g->console, name);
            if (url.empty()) break;
            urls.push_back(std::move(url));
        }
        if (urls.empty()) continue;
        jobs.push_back({stem, stem, std::string(libRoot), std::move(urls), g->console});
    }
    return jobs;
}

int coverJobRank(Console c) {
    if (c == Console::Channel) return kConsoleCount;
    // Generation absteigend, innerhalb einer Generation Heimkonsole vor Handheld (wie buildBarEntries).
    const ConsoleInfo& info = consoleInfo(c);
    int rank = 0;
    for (int i = 0; i < kConsoleCount; ++i) {
        const Console o = static_cast<Console>(i);
        if (o == Console::Channel || o == c) continue;
        const ConsoleInfo& oi = consoleInfo(o);
        if (oi.generation > info.generation || (oi.generation == info.generation && !oi.handheld && info.handheld))
            ++rank;
    }
    return rank;
}

void orderCoverJobs(std::vector<CoverJob>& jobs) {
    std::stable_sort(jobs.begin(), jobs.end(),
                     [](const CoverJob& a, const CoverJob& b) { return coverJobRank(a.console) < coverJobRank(b.console); });
}

std::string missingListPath(std::string_view libRoot) {
    return std::string(libRoot) + "/newlauncher/cache/covers/missing.txt";
}

std::vector<std::string> parseMissingList(std::string_view text) {
    std::vector<std::string> out;
    // M24: Kopfzeile = Sucher-Version; ältere Listen verwerfen, damit der neue Sucher alles neu probiert.
    const std::size_t first = text.find('\n');
    std::string_view head = text.substr(0, first);
    if (!head.empty() && head.back() == '\r') head.remove_suffix(1);
    if (head != kMissingListHeader) return out;
    text = first == std::string_view::npos ? std::string_view() : text.substr(first + 1);
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
    if (stems.empty()) return out;
    out += kMissingListHeader;
    out += '\n';
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

CoverClearResult clearCoverCache(FileSystem& fs, std::string_view libRoot, const std::function<void()>& onRemoved,
                                 const std::function<bool()>& cancelled) {
    CoverClearResult r;
    std::vector<std::string> files;
    for (const std::string& f : existingCoverFiles(fs, libRoot, true)) files.push_back(f);
    const std::string missing = missingListPath(libRoot);
    const std::size_t slash = missing.rfind('/');
    if (auto entries = fs.list(std::string_view(missing).substr(0, slash)); entries.isOk()) {
        for (const DirEntry& e : entries.value())
            if (!e.isDir && e.name == std::string_view(missing).substr(slash + 1)) files.push_back(missing);
    }
    for (const std::string& f : files) {
        if (cancelled && cancelled()) {
            r.cancelled = true;
            break;
        }
        if (fs.removeFile(f).isOk()) {
            ++r.removed;
            if (onRemoved) onRemoved();
        } else {
            ++r.failed;
        }
    }
    return r;
}

DownloadSummary runCoverDownloads(std::span<const CoverJob> jobs, std::span<const std::string> regions,
                                  std::string_view baseUrl, const CoverDownloadIo& io) {
    DownloadSummary sum;
    u32 streak = 0;  // Fehler in Folge
    std::vector<u8> body;
    for (const CoverJob& job : jobs) {
        enum class Res { Missing, Ok, Failed } res = Res::Missing;
        std::string_view okRegion;
        const bool retro = !job.urls.empty();
        const std::span<const std::string> tries = retro ? std::span<const std::string>(job.urls) : regions;
        if (streak >= kMaxConsecutiveDownloadFailures || io.cancelled()) {
            res = Res::Failed;  // nicht mehr versucht
        } else {
            for (const std::string& t : tries) {
                std::string current = retro ? t : gametdbCoverUrl(baseUrl, t, job.id6);
                // M24: libretro-Symlinks kommen als Text mit dem Zielnamen; bis zu kMaxLibretroLinkHops folgen.
                for (int hop = 0; res == Res::Missing; ++hop) {
                    if (io.cancelled()) {
                        res = Res::Failed;
                        break;
                    }
                    const auto url = parseUrl(current);
                    if (!url) {
                        res = Res::Failed;
                        break;
                    }
                    const HttpOutcome o = io.get(*url, body);
                    if (o == HttpOutcome::Failed) {
                        res = Res::Failed;
                        break;
                    }
                    if (o == HttpOutcome::NotFound) break;
                    if (!isPng(body)) {
                        const std::string target = retro && hop < kMaxLibretroLinkHops ? libretroSymlinkTarget(body)
                                                                                         : std::string();
                        if (target.empty()) break;
                        current = libretroLinkUrl(current, target);
                        continue;
                    }
                    if (!io.save(job.stem, body)) {
                        res = Res::Failed;
                        break;
                    }
                    res = Res::Ok;
                    okRegion = retro ? std::string_view("libretro") : std::string_view(t);
                }
                if (res != Res::Missing) break;
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
    return in.enabled && in.ready && !in.scanRunning && !in.netBusy && !in.alreadyRan && !in.clearing;
}

}  // namespace nl
