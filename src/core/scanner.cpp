#include "core/scanner.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include "core/disc_header.hpp"
#include "core/ini.hpp"
#include "core/text_fold.hpp"
#include "core/zip_reader.hpp"

namespace nl {

namespace {

constexpr u64 kCisoHeaderOffset = 0x8000;  // CISO: Blockkarte davor, GC-Kopf dahinter

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string extensionOf(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string() : lower(name.substr(dot));
}

bool hasExtension(std::string_view name, const std::vector<std::string>& exts) {
    const std::string ext = extensionOf(name);
    if (ext.empty()) return false;
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::string_view stemOf(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

bool isId6(std::string_view s) {
    if (s.size() != 6) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// Ersatztitel aus einem Datei- oder Ordnernamen (Spec v2 5.3): „Titel [ID6]“ → Titel, „ID6_Titel“ → Titel,
// „ID6“ allein → leer (dann gilt der Schlüssel), sonst der Name selbst.
std::string titleFromName(std::string_view n) {
    n = trim(n);
    if (n.size() >= 9 && n.back() == ']' && n[n.size() - 8] == '[' && isId6(n.substr(n.size() - 7, 6)))
        return std::string(trim(n.substr(0, n.size() - 8)));
    if (n.size() > 7 && n[6] == '_' && isId6(n.substr(0, 6))) return std::string(trim(n.substr(7)));
    if (isId6(n)) return {};
    return std::string(n);
}

bool hasDir(const std::vector<DirEntry>& entries, std::string_view name) {
    for (const DirEntry& e : entries)
        if (e.isDir && lower(e.name) == name) return true;
    return false;
}

class Scan {
public:
    Scan(FileSystem& fs, std::string_view root, const ScanRules& rules, const IndexData* prev, const MetaDb* meta,
         ScanProgress progress, void* user)
        : fs_(fs), root_(root), rules_(rules), meta_(meta), progress_(progress), user_(user) {
        if (prev != nullptr)
            for (const IndexGame& g : prev->games) prevByPath_[g.path] = &g;
    }

    void console(Console c) {
        for (const std::string& folder : rules_.paths[static_cast<std::size_t>(c)].folders) {
            if (cancelled_) return;
            walk(c, folder, topMtime(folder), 1, "");
        }
    }

    ScanResult finish(const IndexData* prev) {
        std::stable_sort(result_.index.games.begin(), result_.index.games.end(), [](const IndexGame& a, const IndexGame& b) {
            if (a.console != b.console) return a.console < b.console;
            return compareTitles(a.title, b.title) < 0;
        });
        std::set<std::string_view> oldKeys, newKeys;
        if (prev != nullptr)
            for (const IndexGame& g : prev->games) oldKeys.insert(g.key);
        for (const IndexGame& g : result_.index.games) newKeys.insert(g.key);
        for (std::string_view k : newKeys)
            if (!oldKeys.count(k)) ++result_.stats.added;
        for (std::string_view k : oldKeys)
            if (!newKeys.count(k)) ++result_.stats.removed;
        return std::move(result_);
    }

    bool cancelled() const { return cancelled_; }

private:
    // Ordner `dir` auf Ebene `depth` (1 = konfigurierter Ordner) durchgehen; `dirName` ist sein Name ab Ebene 2.
    void walk(Console c, const std::string& dir, u32 mtime, u8 depth, std::string_view dirName) {
        std::vector<DirEntry> entries;
        if (!listSorted(dir, mtime, entries)) return;
        // Entpacktes GC-Abbild (FST): Ordner mit sys/boot.bin ist das Spiel; darunter wird nicht weiter gesucht.
        if (c == Console::Gc && depth >= 2 && hasDir(entries, "sys")) {
            if (auto boot = bootBin(dir)) {
                if (!tick()) return;
                disc(c, dir, boot->first, boot->second, 0, false, titleFromName(dirName));
            }
            return;
        }
        std::set<std::string> names;
        for (const DirEntry& e : entries) names.insert(lower(e.name));
        const ConsolePaths& rules = rules_.paths[static_cast<std::size_t>(c)];
        for (const DirEntry& e : entries) {
            if (cancelled_) return;
            if (e.isDir) {
                if (depth < rules_.maxDepth) walk(c, joinPath(dir, e.name), e.mtime, static_cast<u8>(depth + 1), e.name);
                continue;
            }
            if (c != Console::Gc && c != Console::Wii) {
                rom(c, joinPath(dir, e.name), e, rules);
                continue;
            }
            if (!hasExtension(e.name, rules.extensions)) continue;
            const std::string name = lower(e.name);
            if (c == Console::Gc && name.starts_with("disc2.")) continue;  // gehört zu game.* daneben
            if (!tick()) return;
            u8 flags = 0;
            if (c == Console::Gc && name.starts_with("game.") && names.count("disc2" + extensionOf(name)))
                flags |= kFlagSecondDisc;
            if (c == Console::Wii && names.count(lower(stemOf(e.name)) + ".wbf1")) flags |= kFlagSplitFile;
            std::string fallback = name.starts_with("game.") ? std::string() : titleFromName(stemOf(e.name));
            if (fallback.empty()) fallback = titleFromName(dirName);
            const std::string path = joinPath(dir, e.name);
            disc(c, path, path, e.size, flags, c == Console::Wii && extensionOf(name) == ".wbfs", std::move(fallback));
        }
    }

    void rom(Console c, const std::string& path, const DirEntry& e, const ConsolePaths& rules) {
        if (!hasExtension(e.name, rules.extensions)) return;
        const bool zip = extensionOf(e.name) == ".zip";
        if (zip && (rules_.zipConsoles & (1u << static_cast<unsigned>(c))) == 0) return;
        if (!tick()) return;
        if (zip && !zipHasRom(path, e.size, rules)) {
            ++result_.stats.skipped;
            return;
        }
        IndexGame g{c, 0, std::string(consoleInfo(c).key) + "/" + e.name, "", path, e.size};
        g.title = metaTitle(g).value_or(cleanTitle(stemOf(e.name)));
        add(std::move(g));
    }

    // Ein ZIP zählt, wenn es eine Datei mit einer (Nicht-ZIP-)Endung der Konsole enthält. Bekannte Dateien
    // (gleicher Pfad und gleiche Größe wie im alten Index) werden nicht erneut gelesen.
    bool zipHasRom(const std::string& path, u64 size, const ConsolePaths& rules) {
        auto it = prevByPath_.find(path);
        if (it != prevByPath_.end() && it->second->size == size) return true;
        ++result_.stats.headersRead;
        auto dir = readZipDirectory(fs_, joinPath(root_, path), size);
        if (!dir.isOk()) return false;
        for (const ZipEntry& z : dir.value()) {
            if (z.isDir) continue;
            const std::size_t slash = z.name.rfind('/');
            const std::string_view base =
                slash == std::string::npos ? std::string_view(z.name) : std::string_view(z.name).substr(slash + 1);
            if (extensionOf(base) != ".zip" && hasExtension(base, rules.extensions)) return true;
        }
        return false;
    }

    // Pfad und Größe von <dir>/sys/boot.bin, falls vorhanden.
    std::optional<std::pair<std::string, u64>> bootBin(const std::string& dir) {
        const std::string sys = joinPath(dir, "sys");
        auto r = fs_.list(joinPath(root_, sys));
        if (!r.isOk()) return std::nullopt;
        for (const DirEntry& e : r.value())
            if (!e.isDir && lower(e.name) == "boot.bin") return std::make_pair(joinPath(sys, e.name), e.size);
        return std::nullopt;
    }

    // Listet einen Ordner (relativ zur Wurzel) sortiert und vermerkt ihn mit mtime im Index.
    // Die mtime kommt aus dem Eintrag des Elternordners, den der Aufrufer schon gelistet hat.
    bool listSorted(const std::string& rel, u32 mtime, std::vector<DirEntry>& out) {
        auto r = fs_.list(joinPath(root_, rel));
        if (!r.isOk()) return false;
        out = std::move(r).value();
        std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
        result_.index.folders.push_back(IndexFolder{rel, mtime});
        return true;
    }

    // mtime eines konfigurierten Ordners ("games", "roms/nes"); Elternlisten werden zwischengespeichert.
    u32 topMtime(std::string_view rel) {
        const std::size_t slash = rel.rfind('/');
        const std::string parent = slash == std::string_view::npos ? std::string() : std::string(rel.substr(0, slash));
        const std::string_view name = slash == std::string_view::npos ? rel : rel.substr(slash + 1);
        auto it = parentCache_.find(parent);
        if (it == parentCache_.end()) {
            auto r = fs_.list(parent.empty() ? root_ : joinPath(root_, parent));
            it = parentCache_.emplace(parent, r.isOk() ? std::move(r).value() : std::vector<DirEntry>{}).first;
        }
        for (const DirEntry& e : it->second)
            if (e.isDir && e.name == name) return e.mtime;
        return 0;
    }

    // path: Spielpfad im Index; headerPath: Datei mit dem Disc-Kopf (bei FST sys/boot.bin);
    // fallback: Ersatztitel aus dem Namen, wenn weder Kopf noch Metadaten einen Titel liefern.
    void disc(Console c, std::string path, const std::string& headerPath, u64 size, u8 flags, bool wbfs,
              std::string fallback) {
        IndexGame g{c, flags, "", "", std::move(path), size};
        auto it = prevByPath_.find(g.path);
        const std::optional<std::string> fromMeta = metaTitle(g.console, it != prevByPath_.end() ? it->second->key : "");
        // Ein übernommener Meta-Titel ohne aktuellen Meta-Eintrag ist veraltet: dann den Header neu lesen.
        const bool staleMeta = it != prevByPath_.end() && (it->second->flags & kFlagTitleFromMeta) && !fromMeta;
        if (it != prevByPath_.end() && !staleMeta && it->second->size == size && it->second->console == c) {
            g.key = it->second->key;
            g.title = it->second->title;
            g.flags = static_cast<u8>(flags | (it->second->flags & kFlagTitleFromHeader));
        } else {
            u8 buf[kDiscProbeBytes];
            auto n = fs_.read(joinPath(root_, headerPath), 0, buf);
            ++result_.stats.headersRead;
            if (!n.isOk()) {
                ++result_.stats.skipped;
                return;
            }
            std::span<const u8> head(buf, n.value());
            std::optional<DiscInfo> info = c == Console::Gc ? parseGcIso(head) : (wbfs ? parseWbfs(head) : parseWiiIso(head));
            // CISO: Magic „CISO“, Blockgröße 2 MiB; der GC-Kopf steht hinter der Blockkarte.
            static constexpr u8 kCiso[8] = {'C', 'I', 'S', 'O', 0x00, 0x00, 0x20, 0x00};
            if (!info && c == Console::Gc && head.size() >= 8 && std::memcmp(buf, kCiso, 8) == 0) {
                auto m = fs_.read(joinPath(root_, headerPath), kCisoHeaderOffset, buf);
                if (m.isOk()) info = parseGcIso(std::span<const u8>(buf, m.value()));
            }
            if (!info) {
                ++result_.stats.skipped;
                return;
            }
            g.key = info->id6;
            if (!info->title.empty()) {
                g.title = info->title;
                g.flags |= kFlagTitleFromHeader;
            } else {
                g.title = fallback.empty() ? g.key : std::move(fallback);
            }
        }
        if (auto t = metaTitle(g)) {
            g.title = *t;
            g.flags |= kFlagTitleFromMeta;
        }
        add(std::move(g));
    }

    std::optional<std::string> metaTitle(Console c, std::string_view key) const {
        if (key.empty()) return std::nullopt;
        IndexGame probe{c, 0, std::string(key), "", "", 0};
        return metaTitle(probe);
    }

    std::optional<std::string> metaTitle(const IndexGame& g) const {
        if (meta_ == nullptr) return std::nullopt;
        auto e = meta_->find(metaKey(g));
        if (!e || e->title.empty()) return std::nullopt;
        return std::string(e->title);
    }

    void add(IndexGame g) {
        if (!keys_.insert(g.key).second) {
            ++result_.stats.skipped;  // doppelter Schlüssel: erste Datei gewinnt
            return;
        }
        result_.index.games.push_back(std::move(g));
    }

    // false = abgebrochen; danach liest der Scan nichts mehr.
    bool tick() {
        ++seen_;
        if (progress_ != nullptr && !progress_(user_, seen_)) cancelled_ = true;
        return !cancelled_;
    }

    FileSystem& fs_;
    std::string root_;
    const ScanRules& rules_;
    const MetaDb* meta_;
    ScanProgress progress_;
    void* user_;
    std::unordered_map<std::string_view, const IndexGame*> prevByPath_;
    std::unordered_map<std::string, std::vector<DirEntry>> parentCache_;
    std::set<std::string> keys_;
    ScanResult result_;
    u32 seen_ = 0;
    bool cancelled_ = false;
};

}  // namespace

ScanRules defaultScanRules() {
    ScanRules r;
    for (int i = 0; i < kConsoleCount; ++i) r.paths[static_cast<std::size_t>(i)] = defaultConsolePaths(static_cast<Console>(i));
    return r;
}

Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const ScanRules& rules, const IndexData* previous,
                               const MetaDb* meta, ScanProgress progress, void* user) {
    if (!fs.list(libRoot).isOk()) {
        return Result<ScanResult>::err(Error{ErrorCode::NotMounted, std::string(libRoot) + " nicht lesbar"});
    }
    Scan scan(fs, libRoot, rules, previous, meta, progress, user);
    for (int i = 0; i < kConsoleCount && !scan.cancelled(); ++i) scan.console(static_cast<Console>(i));
    if (scan.cancelled()) return Result<ScanResult>::err(Error{ErrorCode::Cancelled, "Scan abgebrochen"});
    return Result<ScanResult>::ok(scan.finish(previous));
}

Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const IndexData* previous, const MetaDb* meta,
                               ScanProgress progress, void* user) {
    return scanLibrary(fs, libRoot, defaultScanRules(), previous, meta, progress, user);
}

}  // namespace nl
