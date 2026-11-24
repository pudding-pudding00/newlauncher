#include "core/sysinfo.hpp"

#include <array>
#include <cstdio>

namespace nl {

namespace {

struct SysMenuRow {
    u16 version;
    char region;
    const char* label;  // "4.3" oder "vWii"
};

// Quelle: wiibrew.org „System Menu“ (Titelversionen 4.0–4.3) und „vWii“ (Titel 00000007-00000002).
constexpr std::array<SysMenuRow, 18> kSysMenu{{
    {416, 'J', "4.0"}, {417, 'U', "4.0"}, {418, 'E', "4.0"},
    {448, 'J', "4.1"}, {449, 'U', "4.1"}, {450, 'E', "4.1"}, {454, 'K', "4.1"},
    {480, 'J', "4.2"}, {481, 'U', "4.2"}, {482, 'E', "4.2"}, {486, 'K', "4.2"},
    {512, 'J', "4.3"}, {513, 'U', "4.3"}, {514, 'E', "4.3"}, {518, 'K', "4.3"},
    {608, 'J', "vWii"}, {609, 'U', "vWii"}, {610, 'E', "vWii"},
}};

const SysMenuRow* findRow(u16 version) {
    for (const SysMenuRow& r : kSysMenu)
        if (r.version == version) return &r;
    return nullptr;
}

const char* yesNo(bool v, bool de) { return v ? (de ? "ja" : "yes") : (de ? "nein" : "no"); }

std::string gib(u64 bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

const char* platformName(Platform p, bool de) {
    switch (p) {
        case Platform::Wii: return "Wii";
        case Platform::VWii: return "vWii";
        default: return de ? "unbekannt" : "unknown";
    }
}

std::string ciosLine(const CiosEntry& c, u8 chosen) {
    std::string l = "  " + std::to_string(c.slot) + " " + ciosKindName(c.kind);
    if (c.kind == CiosKind::D2x) {
        l += " base=" + std::to_string(c.base);
        if (!c.version.empty()) l += " " + c.version;
    }
    if (chosen != 0 && c.slot == chosen) l += " *";
    return l;
}

std::string deviceLine(const DeviceSummary& d, bool de) {
    if (!d.mounted)
        return "  " + (d.name.empty() ? std::string("-") : d.name) + " " + (d.fs.empty() ? "?" : d.fs) +
               (de ? " nicht eingehängt: " + d.reasonDe : " not mounted: " + d.reasonEn);
    std::string l = "  " + d.name + " " + (d.fs.empty() ? "?" : d.fs) + " ";
    if (d.total == 0) l += "?";
    else l += gib(d.total) + (de ? ", frei " : ", free ") + gib(d.free);
    l += ", " + std::to_string(d.games) + (de ? " Spiele" : " games");
    if (d.readOnly) l += de ? ", nur lesend" : ", read-only";
    return l;
}

}  // namespace

char regionFromSysMenu(u16 version) {
    const SysMenuRow* r = findRow(version);
    return r != nullptr ? r->region : '?';
}

std::string sysMenuLabel(u16 version) {
    if (version == 0) return "?";
    const SysMenuRow* r = findRow(version);
    if (r == nullptr) return "v" + std::to_string(version);
    if (r->label[0] == 'v') return std::string(r->label) + " " + r->region;
    return std::string(r->label) + r->region;
}

Platform platformForSysMenu(u16 version, bool latteDetected) {
    if (latteDetected || (version >= 608 && version <= 610)) return Platform::VWii;
    return version == 0 ? Platform::Unknown : Platform::Wii;
}

std::vector<ResolvedEmuSummary> emuSummaries(const EmulatorTable& resolved) {
    std::vector<ResolvedEmuSummary> out;
    for (const char* sec : {"nes", "snes", "gb", "gbc", "gba", "n64_not64", "n64_wii64", "nintendont"}) {
        const EmulatorEntry* e = resolved.section(sec);
        if (e == nullptr || e->dol.empty()) continue;
        const char* how = e->fromFile ? "ini" : e->profile.empty() ? "default" : "detected";
        out.push_back(ResolvedEmuSummary{sec, e->profile, how, e->dol, e->args});
    }
    return out;
}

std::vector<std::string> sysInfoLines(const SystemInfo& s, bool de) {
    std::vector<std::string> out;
    out.push_back("newlauncher " + (s.launcherVersion.empty() ? std::string("?") : s.launcherVersion));
    out.push_back(std::string(de ? "Plattform: " : "Platform: ") + platformName(s.platform, de));
    std::string menu = (de ? "System-Menü: " : "System Menu: ") + sysMenuLabel(s.sysMenuVersion);
    if (s.sysMenuVersion != 0) menu += " (" + std::to_string(s.sysMenuVersion) + ")";
    out.push_back(menu);
    out.push_back(std::string("Region: ") + s.region);
    out.push_back("Launcher-IOS: IOS" + std::to_string(s.launcherIos));
    out.push_back(std::string("AHBPROT: ") + (s.ahbprot ? (de ? "aus" : "off") : (de ? "an" : "on")));
    out.push_back(std::string(de ? "GameCube-Ports: " : "GameCube ports: ") + yesNo(s.gcPorts, de));
    if (s.cios.empty()) {
        out.push_back(de ? "cIOS: keine" : "cIOS: none");
    } else {
        out.push_back("cIOS:");
        for (const CiosEntry& c : s.cios) out.push_back(ciosLine(c, s.ciosSlot));
    }
    out.push_back(de ? "Geräte:" : "Devices:");
    if (s.devices.empty()) out.push_back(de ? "  keine" : "  none");
    for (const DeviceSummary& d : s.devices) out.push_back(deviceLine(d, de));
    out.push_back(de ? "Emulatoren:" : "Emulators:");
    for (const ResolvedEmuSummary& e : s.emulators)
        out.push_back("  " + e.section + " " + (e.profile.empty() ? "-" : e.profile) + " " + e.how + " " + e.path);
    out.push_back(std::string(de ? "Netzwerk: " : "Network: ") +
                  (s.netUp ? (de ? "verbunden" : "connected") : (de ? "nicht verbunden" : "not connected")));
    out.push_back(std::string(de ? "Uhrzeit per SNTP gestellt: " : "Clock set via SNTP: ") + yesNo(s.timeSynced, de));
    out.push_back(std::string("HTTPS: ") + yesNo(s.tls, de));
    const std::string& nand = de ? s.emuNandDe : s.emuNandEn;
    if (!nand.empty()) out.push_back("EmuNAND: " + nand);
    return out;
}

std::string formatSysInfo(const SystemInfo& s, bool german) {
    std::string t;
    for (const std::string& l : sysInfoLines(s, german)) {
        t += l;
        t += '\n';
    }
    return t;
}

}  // namespace nl
