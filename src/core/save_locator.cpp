#include "core/save_locator.hpp"

#include <cstdio>

#include "core/crc32.hpp"
#include "core/zip_reader.hpp"

namespace nl {
namespace {

constexpr std::size_t kMaxSlug = 40;
constexpr std::size_t kMaxSettingsXml = 64 * 1024;
constexpr int kMaxUnitDepth = 8;  // Wii-Save-Ordner sind flach; mehr Tiefe ist Müll oder eine Schleife

std::string hex8(u32 v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(v));
    return buf;
}

u32 id4Value(std::string_view id4) {
    u32 v = 0;
    for (std::size_t i = 0; i < 4; ++i) v = (v << 8) | (i < id4.size() ? static_cast<u8>(id4[i]) : 0u);
    return v;
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool dirExists(FileSystem& fs, const std::string& dir) { return fs.list(dir).isOk(); }

std::string stripSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();  // "sd:/" → "sd:", Pfade werden mit "/" verbunden
    return s;
}

std::string slugOf(std::string_view stem) {
    std::string out;
    for (char c : stem) {
        c = lower(c);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) out.push_back(c);
        else if (!out.empty() && out.back() != '-') out.push_back('-');
    }
    if (out.size() > kMaxSlug) out.resize(kMaxSlug);
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "rom";
    return out;
}

SaveStore storeOf(std::string_view root) { return root == "nand:" ? SaveStore::RealNand : SaveStore::EmuNand; }

// GX-Einstellungen: <setting name="SaveFolder" ... value="snes9xgx/saves" />; Name ohne Groß/klein.
std::string xmlSaveFolder(std::string_view xml) {
    std::string low(xml);
    for (char& c : low) c = lower(c);
    const std::size_t at = low.find("name=\"savefolder\"");
    if (at == std::string::npos) return {};
    const std::size_t tagEnd = low.find('>', at);
    const std::size_t v = low.find("value=\"", at);
    if (v == std::string::npos || (tagEnd != std::string::npos && v > tagEnd)) return {};
    const std::size_t start = v + 7;
    const std::size_t end = xml.find('"', start);
    if (end == std::string_view::npos) return {};
    return std::string(xml.substr(start, end - start));
}

std::string gxFolder(FileSystem& fs, const std::string& app) {
    for (const std::string& path : {"sd:/apps/" + app + "/settings.xml", "sd:/" + app + "/settings.xml"}) {
        auto raw = fs.readAll(path, kMaxSettingsXml);
        if (!raw.isOk()) continue;
        std::string folder = xmlSaveFolder(
            std::string_view(reinterpret_cast<const char*>(raw.value().data()), raw.value().size()));
        while (!folder.empty() && folder.front() == '/') folder.erase(folder.begin());
        while (!folder.empty() && folder.back() == '/') folder.pop_back();
        // Ein Gerätepräfix oder „..“ im Wert ist kein Ordner, den der Launcher bedienen will.
        if (!folder.empty() && folder.find(':') == std::string::npos && folder.find("..") == std::string::npos)
            return folder;
    }
    return app + "/saves";
}

std::optional<SaveUnit> romUnit(FileSystem& fs, const SaveQuery& q) {
    SaveUnit u;
    u.id = saveUnitId(q);
    u.store = SaveStore::Fat;
    if (!q.saveDir.empty()) {
        u.dir = stripSlash(q.saveDir);
        const std::vector<std::string> def{".sav", ".srm"};
        for (const std::string& ext : q.saveExt.empty() ? def : q.saveExt) u.names.push_back(q.stem + ext);
        return u;
    }
    std::vector<std::string> exts;
    if (q.profile == "fceugx" || q.profile == "vbagx") exts = {".sav"};
    else if (q.profile == "snes9xgx") exts = {".srm", ".rtc"};
    else return std::nullopt;
    const std::string folder = gxFolder(fs, q.profile);
    u.dir = "sd:/" + folder;
    if (!dirExists(fs, u.dir) && dirExists(fs, "usb:/" + folder)) u.dir = "usb:/" + folder;
    for (const std::string& e : exts) u.names.push_back(q.stem + e);
    return u;
}

bool endsWithCi(std::string_view s, std::string_view suf) {
    if (s.size() < suf.size()) return false;
    for (std::size_t i = 0; i < suf.size(); ++i)
        if (lower(s[s.size() - suf.size() + i]) != suf[i]) return false;
    return true;
}

Result<void> collect(FileSystem& fs, const std::string& dir, const std::string& rel, int depth, std::size_t maxRaw,
                     std::size_t& total, std::vector<SaveFile>& out) {
    using R = Result<void>;
    if (depth > kMaxUnitDepth) return R::err(Error{ErrorCode::InvalidData, "Spielstand: zu tief: " + dir});
    auto list = fs.list(dir);
    if (!list.isOk()) return R::err(list.error());
    for (const DirEntry& e : list.value()) {
        const std::string path = dir + "/" + e.name;
        const std::string relPath = rel.empty() ? e.name : rel + "/" + e.name;
        if (e.isDir) {
            if (auto r = collect(fs, path, relPath, depth + 1, maxRaw, total, out); !r.isOk()) return r;
            continue;
        }
        if (e.size > maxRaw - total) return R::err(Error{ErrorCode::InvalidData, "Spielstand größer als erlaubt"});
        auto data = fs.readAll(path, maxRaw - total);
        if (!data.isOk()) return R::err(data.error());
        total += data.value().size();
        out.push_back(SaveFile{relPath, std::move(data.value())});
    }
    return R::ok();
}

}  // namespace

std::string saveUnitId(const SaveQuery& q) {
    switch (q.kind) {
        case SaveKind::WiiDisc: return "wii-" + q.id4;
        case SaveKind::Channel: return "ch-" + hex8(static_cast<u32>(q.titleId >> 32)) + hex8(static_cast<u32>(q.titleId));
        case SaveKind::GcSingle: return "gc-" + q.id4;
        case SaveKind::GcMulti: return q.jp ? "gc-ninmemj" : "gc-ninmem";
        case SaveKind::Rom: {
            const u32 crc = crc32(std::span<const u8>(reinterpret_cast<const u8*>(q.gameKey.data()), q.gameKey.size()));
            return std::string(consoleInfo(q.console).key) + "-" + slugOf(q.stem) + "-" + hex8(crc);
        }
    }
    return {};
}

std::optional<SaveUnit> locateSave(FileSystem& fs, const SaveQuery& q) {
    const std::string root = stripSlash(q.root);
    SaveUnit u;
    u.id = saveUnitId(q);
    switch (q.kind) {
        case SaveKind::WiiDisc: {
            const std::string lo = hex8(id4Value(q.id4));
            u.store = storeOf(root);
            u.dir = root + "/title/00010000/" + lo + "/data";
            const std::string alt = root + "/title/00010004/" + lo + "/data";
            if (!dirExists(fs, u.dir) && dirExists(fs, alt)) u.dir = alt;
            return u;
        }
        case SaveKind::Channel:
            u.store = storeOf(root);
            u.dir = root + "/title/" + hex8(static_cast<u32>(q.titleId >> 32)) + "/" + hex8(static_cast<u32>(q.titleId)) +
                    "/data";
            return u;
        case SaveKind::GcSingle:
        case SaveKind::GcMulti:
            u.store = SaveStore::Fat;
            u.dir = root + "/saves";
            u.names = {q.kind == SaveKind::GcSingle ? q.id4 + ".raw" : (q.jp ? "ninmemj.raw" : "ninmem.raw")};
            return u;
        case SaveKind::Rom: return romUnit(fs, q);
    }
    return std::nullopt;
}

bool isSaveStateName(std::string_view name) {
    for (std::string_view ext : {".frz", ".fcs", ".sgm", ".png"})
        if (endsWithCi(name, ext)) return true;
    if (name.size() >= 4 && endsWithCi(name.substr(0, name.size() - 1), ".st") && name.back() >= '0' && name.back() <= '9')
        return true;
    std::string low(name);
    for (char& c : low) c = lower(c);
    const std::size_t dot = low.rfind('.');
    return dot != std::string::npos && low.compare(dot, 6, ".state") == 0;
}

Result<std::vector<SaveFile>> readUnit(FileSystem& fs, const SaveUnit& u, std::size_t maxRaw) {
    using R = Result<std::vector<SaveFile>>;
    std::vector<SaveFile> out;
    std::size_t total = 0;
    if (u.names.empty()) {
        if (!dirExists(fs, u.dir)) return R::ok(std::move(out));
        if (auto r = collect(fs, u.dir, "", 0, maxRaw, total, out); !r.isOk()) return R::err(r.error());
        return R::ok(std::move(out));
    }
    for (const std::string& name : u.names) {
        auto data = fs.readAll(u.dir + "/" + name, maxRaw - total);
        if (!data.isOk()) {
            if (data.error().code == ErrorCode::NotFound) continue;
            return R::err(data.error());
        }
        total += data.value().size();
        out.push_back(SaveFile{name, std::move(data.value())});
    }
    return R::ok(std::move(out));
}

bool unitAcceptsPath(const SaveUnit& u, std::string_view relPath) {
    if (!isSafeZipPath(relPath) || relPath.back() == '/') return false;
    if (u.names.empty()) return true;
    for (const std::string& n : u.names)
        if (n == relPath) return true;
    return false;
}

}  // namespace nl
