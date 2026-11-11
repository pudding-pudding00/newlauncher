#include "core/emu_catalog.hpp"

#include <array>

#include "core/ini.hpp"

namespace nl {

namespace {

constexpr std::array<EmuInfo, 6> kCatalog = {{
    {"fceugx", "FCE Ultra GX (NES)", "sd:/apps/fceugx/boot.dol"},
    {"snes9xgx", "Snes9x GX (SNES)", "sd:/apps/snes9xgx/boot.dol"},
    {"vbagx", "Visual Boy Advance GX (GB/GBC/GBA)", "sd:/apps/vbagx/boot.dol"},
    {"not64", "Not64 (N64)", "sd:/apps/not64/boot.dol"},
    {"wii64", "Wii64 (N64)", "sd:/apps/wii64/boot.dol"},
    {"Nintendont", "Nintendont (GameCube)", "sd:/apps/Nintendont/boot.dol"},
}};

constexpr std::size_t kBlockLines = 7;

bool parseU32(std::string_view s, u32& out) {
    if (s.empty() || s.size() > 10) return false;
    u64 v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<u64>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<u32>(v);
    return true;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return lines;
}

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

// Erste Zeile eines App-Blocks: mindestens 6 Felder, 2., 4. und 6. numerisch, 5. ist der Typ.
bool parseHead(std::string_view line, OscApp& app) {
    const auto f = fields(line);
    if (f.size() < 6) return false;
    u32 icon = 0;
    if (!parseU32(f[1], app.timestamp) || !parseU32(f[2], icon) || !parseU32(f[3], app.unzippedSize) ||
        !parseU32(f[5], app.zipSize))
        return false;
    app.name = std::string(f[0]);
    return true;
}

std::string withSlash(std::string_view repo) {
    std::string base(repo);
    if (base.empty() || base.back() != '/') base += '/';
    return base;
}

}  // namespace

std::vector<OscApp> parseOscList(std::string_view text) {
    const auto lines = splitLines(text);
    std::vector<OscApp> apps;
    std::size_t i = 0;
    while (i < lines.size()) {
        const std::string_view line = lines[i];
        OscApp app;
        if (line.empty() || line.front() == '=' || line.rfind("Homebrew ", 0) == 0 || !parseHead(line, app)) {
            ++i;
            continue;
        }
        if (i + kBlockLines > lines.size()) break;  // abgeschnitten
        app.displayName = std::string(trim(lines[i + 1]));
        app.version = std::string(trim(lines[i + 3]));
        apps.push_back(std::move(app));
        i += kBlockLines;
    }
    return apps;
}

std::span<const EmuInfo> emulatorCatalog() { return kCatalog; }

std::map<std::string, InstalledEmu> parseInstalledEmus(std::string_view text) {
    const IniDoc ini = IniDoc::parse(text);
    std::map<std::string, InstalledEmu> out;
    for (const EmuInfo& e : kCatalog) {
        InstalledEmu inst;
        if (!parseU32(trim(ini.get(e.oscName, "timestamp")), inst.timestamp)) continue;
        inst.version = std::string(ini.get(e.oscName, "version"));
        out.emplace(std::string(e.oscName), std::move(inst));
    }
    return out;
}

std::string formatInstalledEmus(const std::map<std::string, InstalledEmu>& m) {
    std::string out = "; von newlauncher geschrieben (Spec 6.9)\n";
    for (const auto& [name, inst] : m) {
        out += "[" + name + "]\n";
        out += "timestamp = " + std::to_string(inst.timestamp) + "\n";
        out += "version = " + inst.version + "\n";
    }
    return out;
}

std::vector<EmuRow> emuRows(const std::map<std::string, InstalledEmu>& installed,
                            const std::function<bool(std::string_view dol)>& dolExists, std::span<const OscApp> list) {
    std::vector<EmuRow> rows;
    for (const EmuInfo& e : kCatalog) {
        EmuRow r;
        r.info = &e;
        for (const OscApp& a : list)
            if (a.name == e.oscName) r.remote = a;
        if (!dolExists(e.dol)) {
            r.state = EmuState::NotInstalled;
        } else if (auto it = installed.find(std::string(e.oscName)); it == installed.end()) {
            r.state = EmuState::UnknownVersion;
        } else {
            r.installed = it->second;
            r.state = (r.remote && r.remote->timestamp > it->second.timestamp) ? EmuState::UpdateAvailable
                                                                                : EmuState::Current;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

std::string oscListUrl(std::string_view repo) { return withSlash(repo) + "listv036.txt"; }

std::string oscZipUrl(std::string_view repo, std::string_view name) {
    return withSlash(repo) + std::string(name) + "/" + std::string(name) + ".zip";
}

}  // namespace nl
