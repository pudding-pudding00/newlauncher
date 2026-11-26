#include "core/emu_profiles.hpp"

#include <array>

#include "core/ini.hpp"

namespace nl {

namespace {

constexpr u8 bit(Console c) { return static_cast<u8>(1u << static_cast<unsigned>(c)); }

constexpr u8 kFsFat = fsBit(FsKind::Fat);  // M16: alle eingebauten Profile lesen nur FAT

constexpr u8 kDefaultZipConsoles = 0b0101'0111;  // wie ScanRules: Nes, Snes, Gb, Gbc, Gba

// Spike M13 (docs/superpowers/spikes/m13-retroarch.md): RetroArch verbindet argv[1] und argv[2] → {dir}|{file}.
constexpr std::array<EmuProfile, 10> kProfiles{{
    {"fceugx", "fceugx", "boot.dol", "FCE Ultra GX", bit(Console::Nes), "{dir}|{file}", true, kFsFat},
    {"snes9xgx", "snes9xgx", "boot.dol", "Snes9x GX", bit(Console::Snes), "{dir}|{file}", true, kFsFat},
    {"vbagx", "vbagx", "boot.dol", "Visual Boy Advance GX",
     static_cast<u8>(bit(Console::Gb) | bit(Console::Gbc) | bit(Console::Gba)), "{dir}|{file}", true, kFsFat},
    {"not64", "not64", "boot.dol", "Not64", bit(Console::N64), "rompath=\"{path}\"", false, kFsFat},  // argv = Schlüssel=Wert
    {"wii64", "wii64", "boot.dol", "Wii64", bit(Console::N64), "{path}", false, kFsFat},
    {"nintendont", "nintendont", "boot.dol", "Nintendont", bit(Console::Gc), "", false, kFsFat},
    {"retroarch-fceumm", "retroarch-wii", "fceumm_libretro_wii.dol", nullptr, bit(Console::Nes), "{dir}|{file}",
     false, kFsFat},
    {"retroarch-snes9x2010", "retroarch-wii", "snes9x2010_libretro_wii.dol", nullptr, bit(Console::Snes),
     "{dir}|{file}", false, kFsFat},
    {"retroarch-gambatte", "retroarch-wii", "gambatte_libretro_wii.dol", nullptr,
     static_cast<u8>(bit(Console::Gb) | bit(Console::Gbc)), "{dir}|{file}", false, kFsFat},
    {"retroarch-mgba", "retroarch-wii", "mgba_libretro_wii.dol", nullptr, bit(Console::Gba), "{dir}|{file}", false,
     kFsFat},
}};

constexpr std::array<const char*, 2> kOrderNes{"fceugx", "retroarch-fceumm"};
constexpr std::array<const char*, 2> kOrderSnes{"snes9xgx", "retroarch-snes9x2010"};
constexpr std::array<const char*, 2> kOrderGb{"vbagx", "retroarch-gambatte"};
constexpr std::array<const char*, 2> kOrderGba{"vbagx", "retroarch-mgba"};
constexpr std::array<const char*, 1> kOrderNot64{"not64"};
constexpr std::array<const char*, 1> kOrderWii64{"wii64"};
constexpr std::array<const char*, 1> kOrderNintendont{"nintendont"};

struct Section {
    const char* name;
    u8 consoleBit;
    std::span<const char* const> order;
};

constexpr Console kRomConsoles[] = {Console::Nes, Console::Snes, Console::Gb, Console::Gbc, Console::Gba};

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && equalsNoCase(s.substr(s.size() - suffix.size()), suffix);
}

std::string_view fileName(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool dolMatches(const EmuProfile& p, const FoundApp& a) {
    const std::string_view file = fileName(a.dol);
    if (equalsNoCase(file, p.dolName)) return true;
    return std::string_view(p.dolName) == "boot.dol" && equalsNoCase(file, "boot.elf");
}

// Erst nach Ordnername, dann nach meta.xml-Name (umbenannte Ordner); bei Gleichstand gewinnt der frühere Fund.
const FoundApp* findApp(const EmuProfile& p, std::span<const FoundApp> found) {
    for (const FoundApp& a : found)
        if (equalsNoCase(a.folder, p.folder) && dolMatches(p, a)) return &a;
    if (p.metaName == nullptr) return nullptr;
    for (const FoundApp& a : found)
        if (!a.name.empty() && equalsNoCase(a.name, p.metaName) && dolMatches(p, a)) return &a;
    return nullptr;
}

const char* profileOfDol(std::string_view dol, std::span<const FoundApp> found) {
    for (const FoundApp& a : found) {
        if (!equalsNoCase(a.dol, dol)) continue;
        for (const EmuProfile& p : kProfiles)
            if (findApp(p, std::span<const FoundApp>(&a, 1)) != nullptr) return p.id;
        return "";
    }
    return nullptr;
}

ResolvedEmu fromProfile(const EmuProfile& p, const FoundApp& a, bool bad) {
    return ResolvedEmu{a.dol, p.args, p.id, false, bad, 0};
}

ResolvedEmu resolveSection(const Section& s, const EmulatorEntry& e, std::span<const FoundApp> found,
                           std::optional<std::string_view> gameProfile) {
    // 1. eigener Abschnitt (nicht die v1-Vorgabe): stärkste Stufe. Zeigt er auf ein bekanntes Profil (z. B. einen
    //    RetroArch-Kern), trägt er dessen id, damit zipConsoles dessen ZIP-Fähigkeit kennt.
    if (e.fromFile && !isV1Default(s.name, e.dol)) {
        const char* id = profileOfDol(e.dol, found);
        return ResolvedEmu{e.dol, e.args, id != nullptr ? id : "", true, false, 0};
    }
    // 2. games.ini [emulator]
    bool bad = false;
    if (gameProfile) {
        const EmuProfile* p = findProfile(*gameProfile);
        if (p == nullptr || (p->consoleMask & s.consoleBit) == 0) {
            bad = true;
        } else if (const FoundApp* a = findApp(*p, found)) {
            return fromProfile(*p, *a, false);
        }
    }
    // Ab hier ist e die v1-Vorgabe: v1 schrieb sie immer, sie ist keine Nutzerentscheidung → fromIni = false.
    // 3. v1-Vorgabe, deren Datei es gibt: bleibt, startet aber mit dem Schema des Profils (v1 schrieb für Not64
    //    noch „{path}“, das Not64 als Schlüssel=Wert-Paar verwirft und dann nur das Menü zeigt).
    if (const char* id = profileOfDol(e.dol, found)) {
        const EmuProfile* p = findProfile(id);
        return ResolvedEmu{e.dol, p != nullptr ? std::string(p->args) : e.args, id, false, bad, 0};
    }
    // 4. Profilreihenfolge
    for (const char* id : s.order) {
        const EmuProfile* p = findProfile(id);
        if (p == nullptr) continue;
        if (const FoundApp* a = findApp(*p, found)) return fromProfile(*p, *a, bad);
    }
    // 5. nichts installiert: v1-Vorgabe bleibt (Preflight meldet dann „Emulator fehlt“)
    return ResolvedEmu{e.dol, e.args, "", false, bad, 0};
}

std::span<const char* const> orderOf(Console c) {
    switch (c) {
        case Console::Nes: return kOrderNes;
        case Console::Snes: return kOrderSnes;
        case Console::Gb:
        case Console::Gbc: return kOrderGb;
        case Console::Gba: return kOrderGba;
        default: return {};
    }
}

std::string_view trimWs(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool isAppDol(std::string_view file) {
    return equalsNoCase(file, "boot.dol") || equalsNoCase(file, "boot.elf") ||
           endsWithNoCase(file, "_libretro_wii.dol");
}

}  // namespace

std::span<const EmuProfile> builtinProfiles() { return kProfiles; }

const EmuProfile* findProfile(std::string_view id) {
    for (const EmuProfile& p : kProfiles)
        if (equalsNoCase(id, p.id)) return &p;
    return nullptr;
}

std::span<const char* const> profileOrder(Console c) { return orderOf(c); }

std::optional<ResolvedEmu> resolveEmulator(Console c, const EmulatorTable& ini, std::span<const FoundApp> found,
                                           std::optional<std::string_view> gameProfile) {
    const EmulatorEntry* e = ini.forConsole(c);
    if (e == nullptr) return std::nullopt;
    ResolvedEmu r = resolveSection(Section{consoleInfo(c).key, bit(c), orderOf(c)}, *e, found, gameProfile);
    r.fsMask = e->fsMask;  // M16: fs gehört zum Abschnitt, gleich welcher Emulator gewählt wird
    return r;
}

EmulatorTable resolveTable(const EmulatorTable& ini, std::span<const FoundApp> found) {
    EmulatorTable out = ini;
    auto apply = [&](const Section& s) {
        const EmulatorEntry* e = ini.section(s.name);
        if (e == nullptr) return;
        const ResolvedEmu r = resolveSection(s, *e, found, std::nullopt);
        EmulatorEntry entry = *e;  // fs, save_dir und save_ext gehören zum Abschnitt, gleich welcher Emulator gewählt wird
        entry.dol = r.dol;
        entry.args = r.args;
        entry.fromFile = r.fromIni;
        entry.profile = r.profile;
        out.set(s.name, std::move(entry));
    };
    for (Console c : kRomConsoles) apply(Section{consoleInfo(c).key, bit(c), orderOf(c)});
    apply(Section{"n64_not64", bit(Console::N64), kOrderNot64});
    apply(Section{"n64_wii64", bit(Console::N64), kOrderWii64});
    apply(Section{"nintendont", bit(Console::Gc), kOrderNintendont});
    return out;
}

u8 emulatorFsMask(const EmulatorEntry& e) {
    if (e.fsMask != 0) return e.fsMask;
    if (const EmuProfile* p = e.profile.empty() ? nullptr : findProfile(e.profile)) return p->fsMask;
    return kFsFat;
}

u8 zipConsoles(const EmulatorTable& resolved) {
    u8 mask = kDefaultZipConsoles;
    for (Console c : kRomConsoles) {
        const EmulatorEntry* e = resolved.forConsole(c);
        if (e == nullptr || e->profile.empty()) continue;
        const EmuProfile* p = findProfile(e->profile);
        if (p == nullptr) continue;
        if (p->acceptsZip) mask = static_cast<u8>(mask | bit(c));
        else mask = static_cast<u8>(mask & ~bit(c));
    }
    return mask;
}

bool isV1Default(std::string_view section, std::string_view dol) {
    const std::string_view v1 = v1DefaultDol(section);
    return !v1.empty() && v1 == dol;
}

std::string metaXmlName(std::string_view xml) {
    const std::size_t open = xml.find("<name>");
    if (open == std::string_view::npos) return {};
    const std::size_t start = open + 6;
    const std::size_t close = xml.find("</name>", start);
    if (close == std::string_view::npos) return {};
    return std::string(trimWs(xml.substr(start, close - start)));
}

std::vector<FoundApp> scanApps(FileSystem& fs, std::string_view device) {
    std::vector<FoundApp> out;
    const std::string apps = std::string(device) + ":/apps";
    auto dirs = fs.list(apps);
    if (!dirs.isOk()) return out;
    std::size_t folders = 0;
    for (const DirEntry& d : dirs.value()) {
        if (!d.isDir || d.name == "." || d.name == "..") continue;
        if (folders++ >= kMaxAppFolders) break;
        const std::string dir = apps + "/" + d.name;
        auto files = fs.list(dir);
        if (!files.isOk()) continue;
        const std::size_t first = out.size();
        bool hasMeta = false;
        for (const DirEntry& f : files.value()) {
            if (f.isDir) continue;
            if (equalsNoCase(f.name, "meta.xml")) hasMeta = true;
            if (isAppDol(f.name)) out.push_back(FoundApp{std::string(device), d.name, dir + "/" + f.name, {}});
        }
        if (!hasMeta || out.size() == first) continue;
        auto meta = fs.readAll(dir + "/meta.xml", kMaxMetaXmlBytes);
        if (!meta.isOk()) continue;
        const auto& b = meta.value();
        const std::string name = metaXmlName(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
        for (std::size_t i = first; i < out.size(); ++i) out[i].name = name;
    }
    return out;
}

}  // namespace nl
