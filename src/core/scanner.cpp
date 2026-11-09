#include "core/scanner.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>

#include "core/disc_header.hpp"
#include "core/text_fold.hpp"

namespace nl {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool hasExtension(std::string_view name, const ConsoleInfo& info) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    const std::string ext = lower(name.substr(dot));
    for (const char* e : info.extensions)
        if (e != nullptr && ext == e) return true;
    return false;
}

std::string_view stemOf(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

class Scan {
public:
    Scan(FileSystem& fs, std::string_view root, const IndexData* prev, const MetaDb* meta, ScanProgress progress, void* user)
        : fs_(fs), root_(root), meta_(meta), progress_(progress), user_(user) {
        if (prev != nullptr)
            for (const IndexGame& g : prev->games) prevByPath_[g.path] = &g;
    }

    void roms(Console c) {
        const ConsoleInfo& info = consoleInfo(c);
        std::vector<DirEntry> entries;
        if (!listSorted(info.folder, topMtime(info.folder), entries)) return;
        for (const DirEntry& e : entries) {
            if (e.isDir || !hasExtension(e.name, info)) continue;
            if (!tick()) return;
            IndexGame g{c, 0, std::string(info.key) + "/" + e.name, "", joinPath(info.folder, e.name), e.size};
            g.title = metaTitle(g).value_or(cleanTitle(stemOf(e.name)));
            add(std::move(g));
        }
    }

    void discs(Console c) {
        const ConsoleInfo& info = consoleInfo(c);
        std::vector<DirEntry> subdirs;
        if (!listSorted(info.folder, topMtime(info.folder), subdirs)) return;
        for (const DirEntry& d : subdirs) {
            if (cancelled_) return;
            if (!d.isDir) continue;
            const std::string dir = joinPath(info.folder, d.name);
            std::vector<DirEntry> files;
            if (!listSorted(dir, d.mtime, files)) continue;
            std::set<std::string> names;
            for (const DirEntry& f : files) names.insert(lower(f.name));
            for (const DirEntry& f : files) {
                if (f.isDir) continue;
                const std::string name = lower(f.name);
                const bool isGame = c == Console::Gc ? name == "game.iso" : hasExtension(f.name, info);
                if (!isGame) continue;
                if (!tick()) return;
                u8 flags = 0;
                if (c == Console::Gc && names.count("disc2.iso")) flags |= kFlagSecondDisc;
                if (c == Console::Wii && names.count(lower(stemOf(f.name)) + ".wbf1")) flags |= kFlagSplitFile;
                disc(c, joinPath(dir, f.name), f.size, flags, name.size() > 5 && name.ends_with(".wbfs"));
            }
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

private:
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

    // mtime eines Ordners erster oder zweiter Ebene ("games", "roms/nes"); Elternlisten werden zwischengespeichert.
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

    void disc(Console c, std::string path, u64 size, u8 flags, bool wbfs) {
        IndexGame g{c, flags, "", "", std::move(path), size};
        auto it = prevByPath_.find(g.path);
        if (it != prevByPath_.end() && it->second->size == size && it->second->console == c) {
            g.key = it->second->key;
            g.title = it->second->title;
            g.flags = static_cast<u8>(flags | (it->second->flags & kFlagTitleFromHeader));
        } else {
            u8 buf[kDiscProbeBytes];
            auto n = fs_.read(joinPath(root_, g.path), 0, buf);
            ++result_.stats.headersRead;
            if (!n.isOk()) {
                ++result_.stats.skipped;
                return;
            }
            const std::span<const u8> head(buf, n.value());
            std::optional<DiscInfo> info = c == Console::Gc ? parseGcIso(head) : (wbfs ? parseWbfs(head) : parseWiiIso(head));
            if (!info) {
                ++result_.stats.skipped;
                return;
            }
            g.key = info->id6;
            if (!info->title.empty()) {
                g.title = info->title;
                g.flags |= kFlagTitleFromHeader;
            } else {
                g.title = g.key;
            }
        }
        if (auto t = metaTitle(g)) g.title = *t;
        add(std::move(g));
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

public:
    bool cancelled() const { return cancelled_; }

private:

    FileSystem& fs_;
    std::string root_;
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

Result<ScanResult> scanLibrary(FileSystem& fs, std::string_view libRoot, const IndexData* previous, const MetaDb* meta,
                               ScanProgress progress, void* user) {
    if (!fs.list(libRoot).isOk()) {
        return Result<ScanResult>::err(Error{ErrorCode::NotMounted, std::string(libRoot) + " nicht lesbar"});
    }
    Scan scan(fs, libRoot, previous, meta, progress, user);
    for (int i = 0; i < kConsoleCount && !scan.cancelled(); ++i) {
        const Console c = static_cast<Console>(i);
        if (c == Console::Gc || c == Console::Wii) scan.discs(c);
        else scan.roms(c);
    }
    if (scan.cancelled()) return Result<ScanResult>::err(Error{ErrorCode::Cancelled, "Scan abgebrochen"});
    return Result<ScanResult>::ok(scan.finish(previous));
}

}  // namespace nl
