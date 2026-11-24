#include "core/game_options.hpp"

#include <array>
#include <span>

#include "core/cios_scan.hpp"

namespace nl {

namespace {

using Values = std::span<const std::string_view>;

// Tabellen Spec 7.2/7.3; die Position eines Werts ist sein Index für die Abbildung.
constexpr std::string_view kWiiVideo[] = {"game", "system", "pal50", "pal60", "ntsc", "progressive"};
constexpr std::string_view kWiiVideoPatch[] = {"off", "normal", "more", "all"};
constexpr std::string_view kSwitch[] = {"0", "1"};
constexpr std::string_view kWiiLanguage[] = {"game", "ja", "en", "de", "fr", "es", "it", "nl", "zh_s", "zh_t", "ko"};
constexpr std::string_view kWiiAspect[] = {"game", "4:3", "16:9"};
constexpr std::string_view kWiiDeflicker[] = {"game", "off", "off_ext", "low", "medium", "high"};
constexpr std::string_view kWiiWidth[] = {"auto", "framebuffer"};
constexpr std::string_view kWiiOnline[] = {"off", "nossl", "wiimmfi"};
constexpr std::string_view kWiiIos[] = {"auto"};             // dazu 200–255
constexpr std::string_view kWiiReturnTo[] = {"none", "hbc"};  // dazu 16 Hexziffern
constexpr std::string_view kWiiHook[] = {"vbi", "kpad", "joypad", "gxdraw", "gxflush", "ossleep", "axframe"};
constexpr std::string_view kWiiSave[] = {"real", "emu"};

constexpr std::string_view kGcVideo[] = {"auto", "pal50", "pal60", "ntsc", "mpal", "none"};
constexpr std::string_view kGcMemcard[] = {"emu", "multi", "real"};
constexpr std::string_view kGcBlocks[] = {"59", "123", "251", "507", "1019", "2043"};
constexpr std::string_view kGcLanguage[] = {"auto", "en", "de", "fr", "es", "it", "nl"};
constexpr std::string_view kGcPads[] = {"1", "2", "3", "4"};

enum class Special : u8 { None, Ios, ReturnTo };

struct Def {
    std::string_view key;
    Values values;
    std::size_t def;
    Special special = Special::None;
};

constexpr std::array<Def, 16> kWiiDefs{{
    {"video", kWiiVideo, 1},
    {"video_patch", kWiiVideoPatch, 0},
    {"vipatch", kSwitch, 0},
    {"language", kWiiLanguage, 0},
    {"country_patch", kSwitch, 0},
    {"region_patch", kSwitch, 0},
    {"aspect", kWiiAspect, 0},
    {"deflicker", kWiiDeflicker, 0},
    {"width", kWiiWidth, 0},
    {"fix480p", kSwitch, 0},
    {"online", kWiiOnline, 0},
    {"cheats", kSwitch, 0},
    {"hook", kWiiHook, 0},
    {"ios", kWiiIos, 0, Special::Ios},
    {"return_to", kWiiReturnTo, 0, Special::ReturnTo},
    {"save", kWiiSave, 0},
}};

constexpr std::array<Def, 11> kGcDefs{{
    {"video", kGcVideo, 0},
    {"progressive", kSwitch, 0},
    {"force_wide", kSwitch, 0},
    {"wiiu_wide", kSwitch, 0},
    {"memcard", kGcMemcard, 0},
    {"memcard_blocks", kGcBlocks, 2},
    {"native_si", kSwitch, 0},
    {"language", kGcLanguage, 0},
    {"max_pads", kGcPads, 3},
    {"skip_ipl", kSwitch, 0},
    {"cheats", kSwitch, 0},
}};

constexpr std::string_view kGamePrefix = "game ";

char lowerChar(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lowerChar(c);
    return out;
}

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lowerChar(a[i]) != lowerChar(b[i])) return false;
    return true;
}

bool isGameSection(std::string_view name, std::string_view id6) {
    return name.size() > kGamePrefix.size() && ieq(name.substr(0, kGamePrefix.size()), kGamePrefix) &&
           ieq(trim(name.substr(kGamePrefix.size())), id6);
}

// Wert des in der Datei zuletzt stehenden Eintrags des Schlüssels über alle passenden Abschnitte.
template <typename Match>
std::optional<std::string> lookup(const IniDoc& doc, Match match, std::string_view key) {
    std::optional<std::string> out;
    std::size_t best = 0;  // Dateiposition; gleichnamige Abschnitte in anderer Schreibweise liegen getrennt
    for (std::string_view name : doc.sectionNames()) {
        if (!match(name)) continue;
        const auto entries = doc.entries(name);
        const auto order = doc.entryOrder(name);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!ieq(entries[i].first, key) || (out && order[i] < best)) continue;
            out = lower(trim(entries[i].second));
            best = order[i];
        }
    }
    return out;
}

bool isHex(std::string_view s) {
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool valid(const Def& d, std::string_view v) {
    for (std::string_view x : d.values)
        if (x == v) return true;
    if (d.special == Special::Ios) return parseIosValue(v).has_value();
    if (d.special == Special::ReturnTo) return v.size() == 16 && isHex(v) && v.substr(0, 8) == "00010001";
    return false;
}

struct Resolved {
    std::string value;
    bool inherited = true;
    std::string inheritedValue;
};

// Spiel > global > Standard; ein unbekannter Wert (auf welcher Ebene auch immer) nimmt den Standard.
Resolved resolve(const Def& d, std::string_view globalSection, const IniDoc& config, const IniDoc& games,
                 std::string_view id6, std::vector<std::string>& bad, bool vWii = false) {
    const std::string standard(vWii && d.key == "wiiu_wide" ? std::string_view("1") : d.values[d.def]);
    auto checked = [&](std::optional<std::string> v) -> std::optional<std::string> {
        if (!v || *v == "default") return std::nullopt;
        if (valid(d, *v)) return v;
        bad.emplace_back(d.key);
        return standard;
    };
    Resolved r;
    const auto global = checked(lookup(config, [&](std::string_view n) { return ieq(n, globalSection); }, d.key));
    r.inheritedValue = global.value_or(standard);
    if (!id6.empty()) {
        if (auto game = checked(lookup(games, [&](std::string_view n) { return isGameSection(n, id6); }, d.key))) {
            r.value = *game;
            r.inherited = false;
            return r;
        }
    }
    r.value = r.inheritedValue;
    r.inherited = !id6.empty();
    return r;
}

std::size_t indexOf(const Def& d, std::string_view v) {
    for (std::size_t i = 0; i < d.values.size(); ++i)
        if (d.values[i] == v) return i;
    return d.def;
}

u32 parseHex32(std::string_view s) {
    u32 v = 0;
    for (char c : s) v = (v << 4) | static_cast<u32>(c <= '9' ? c - '0' : c - 'a' + 10);
    return v;
}

}  // namespace

WiiOptions resolveWiiOptions(const IniDoc& config, const IniDoc& games, std::string_view id6) {
    WiiOptions o;
    WiiCfgFields& c = o.cfg;
    for (const Def& d : kWiiDefs) {
        const Resolved r = resolve(d, "wii", config, games, id6, o.bad);
        const auto i = static_cast<u8>(indexOf(d, r.value));
        if (d.key == "video") c.vidMode = i;
        else if (d.key == "video_patch") c.patchVidMode = i;
        else if (d.key == "vipatch") c.vipatch = i;
        else if (d.key == "language") c.language = i == 0 ? u8{0xCD} : static_cast<u8>(i - 1);
        else if (d.key == "country_patch") c.countryString = i;
        else if (d.key == "region_patch") c.patchregion = i;
        else if (d.key == "aspect") c.aspectRatio = static_cast<s8>(i - 1);
        else if (d.key == "deflicker") c.deflicker = i;
        else if (d.key == "width") c.videoWidth = i;
        else if (d.key == "fix480p") c.patchFix480p = i;
        else if (d.key == "online") c.privateServer = i;
        else if (d.key == "cheats") o.cheats = i == 1;
        else if (d.key == "hook") o.hookType = static_cast<u8>(i + 1);
        else if (d.key == "ios") o.ios = parseIosValue(r.value);
        else if (d.key == "save") o.saveEmu = i == 1;
        else if (d.key == "return_to") {
            if (r.value == "hbc") {
                c.returnTo = kReturnToHbc;
                o.returnToHbc = true;
            } else if (r.value != "none") {
                c.returnTo = parseHex32(std::string_view(r.value).substr(8));
            }
        }
    }
    return o;
}

GcOptions resolveGcOptions(const IniDoc& config, const IniDoc& games, std::string_view id6, NinVersion nin, bool vWii) {
    GcOptions o;
    NinConfigInput& n = o.nin;
    for (const Def& d : kGcDefs) {
        const Resolved r = resolve(d, "gc", config, games, id6, o.bad, vWii);
        if (vWii && (d.key == "native_si" || (d.key == "memcard" && r.value == "real"))) continue;
        const std::size_t i = indexOf(d, r.value);
        if (i == d.def) continue;  // Standard: nichts zu setzen, nie „nicht unterstützt“
        bool supported = true;
        for (const NinOptionGate& g : ninOptionGates())
            if (d.key == g.key && nin < g.minVersion) supported = false;
        if (!supported) {
            o.unsupported.emplace_back(d.key);
            continue;
        }
        if (d.key == "video") {
            constexpr u32 kModes[] = {kNinVidAuto, kNinVidForce | kNinVidPal50, kNinVidForce | kNinVidPal60,
                                      kNinVidForce | kNinVidNtsc, kNinVidForce | kNinVidMpal, kNinVidNone};
            n.videoMode = kModes[i];
        } else if (d.key == "progressive") n.extraFlags |= kNinCfgForceProg;
        else if (d.key == "force_wide") n.extraFlags |= kNinCfgForceWide;
        else if (d.key == "wiiu_wide") n.extraFlags |= kNinCfgWiiuWide;
        else if (d.key == "memcard") {
            if (r.value == "multi") n.extraFlags |= kNinCfgMcMulti;
            if (r.value == "real") {
                n.memcardEmu = false;
                o.memcardReal = true;
            }
        } else if (d.key == "memcard_blocks") n.memcardBlocks = static_cast<u32>(i);
        else if (d.key == "native_si") {
            n.extraFlags |= kNinCfgNativeSi;
            o.nativeSi = true;
        } else if (d.key == "language") n.language = static_cast<u32>(i - 1);
        else if (d.key == "max_pads") n.maxPads = static_cast<u32>(i + 1);
        else if (d.key == "skip_ipl") n.extraFlags |= kNinCfgSkipIpl;
        else if (d.key == "cheats") o.cheats = true;
    }
    return o;
}

u32 hbcReturnTo(std::span<const u64> installedTitles) {
    constexpr u64 kChannel = 0x0001000100000000ull;
    bool old = false;
    for (u64 t : installedTitles) {
        if (t == (kChannel | kReturnToHbc)) return kReturnToHbc;
        old = old || t == (kChannel | kReturnToHbcOld);
    }
    return old ? kReturnToHbcOld : 0;
}

std::vector<OptionValue> listOptions(Console c, const IniDoc& config, const IniDoc& games, std::string_view id6, bool vWii) {
    std::vector<OptionValue> out;
    std::vector<std::string> ignored;
    auto add = [&](std::span<const Def> defs, std::string_view section, bool useVWii) {
        for (const Def& d : defs) {
            Resolved r = resolve(d, section, config, games, id6, ignored, useVWii);
            out.push_back({d.key, std::move(r.value), r.inherited, std::move(r.inheritedValue)});
        }
    };
    if (c == Console::Wii) add(kWiiDefs, "wii", false);
    else if (c == Console::Gc) add(kGcDefs, "gc", vWii);
    return out;
}

std::vector<std::string_view> optionValues(Console c, std::string_view key) {
    const std::span<const Def> defs = c == Console::Wii ? std::span<const Def>(kWiiDefs)
                                      : c == Console::Gc ? std::span<const Def>(kGcDefs)
                                                         : std::span<const Def>{};
    for (const Def& d : defs)
        if (d.key == key) return {d.values.begin(), d.values.end()};
    return {};
}

}  // namespace nl
