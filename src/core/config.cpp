#include "core/config.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include "core/cios_scan.hpp"
#include "core/emu_catalog.hpp"
#include "core/http.hpp"
#include "core/theme_file.hpp"

namespace nl {

namespace {

// Ganzzahl im Bereich [lo, hi], sonst fallback.
int parseIntIn(std::string_view s, int lo, int hi, int fallback) {
    s = trim(s);
    if (s.empty() || s.size() > 6) return fallback;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return fallback;
        v = v * 10 + (c - '0');
    }
    return (v >= lo && v <= hi) ? v : fallback;
}

}  // namespace

int parseClock(std::string_view hhmm, int fallback) {
    hhmm = trim(hhmm);
    const std::size_t colon = hhmm.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 2) return fallback;
    const std::string_view m = hhmm.substr(colon + 1);
    if (m.empty() || m.size() > 2) return fallback;
    const int h = parseIntIn(hhmm.substr(0, colon), 0, 23, -1);
    const int mm = parseIntIn(m, 0, 59, -1);
    if (h < 0 || mm < 0) return fallback;
    return h * 60 + mm;
}

namespace {
// Offizielle Quelle (http- oder https-Form, Groß-/Kleinschreibung egal) → Standard des Builds; sonst unverändert.
std::string sourceForBuild(std::string_view value, std::string_view http, std::string_view https,
                           std::string_view buildDefault) {
    const auto same = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    if (!parseUrl(value) || same(value, http) || same(value, https)) return std::string(buildDefault);
    return std::string(value);
}
}  // namespace

void applyDebugIni(Config& c, const IniDoc& ini) {
    c.debugEmuRepo = std::string(trim(ini.get("debug", "emu_repo")));
    c.debugEmuInstall = std::string(trim(ini.get("debug", "emu_install")));
    c.debugAutostart = std::string(ini.get("debug", "autostart"));  // nur für Tests (Spec 9)
    c.debugCoverUrl = std::string(ini.get("debug", "cover_url"));
    c.debugCheatUrl = std::string(trim(ini.get("debug", "cheat_url")));
    c.debugCheatFetch = std::string(trim(ini.get("debug", "cheat_fetch")));
    c.debugFetchCovers = parseIntIn(ini.get("debug", "fetch_covers"), 0, 1, 0) == 1;
    c.debugSysinfoSave = parseIntIn(ini.get("debug", "sysinfo_save"), 0, 1, 0) == 1;
    c.debugLogSounds = parseIntIn(ini.get("debug", "log_sounds"), 0, 1, 0) == 1;
    c.debugListGames = parseIntIn(ini.get("debug", "list_games"), 0, 1, 0) == 1;
    c.debugSdAsDisk = parseIntIn(ini.get("debug", "sd_as_disk"), 0, 1, 0) == 1;
    c.debugPerf = parseIntIn(ini.get("debug", "perf"), 0, 1, 0) == 1;
    c.debugRemote = parseIntIn(ini.get("debug", "remote"), 0, 1, 0) == 1;
    c.debugUpdateUrl = std::string(trim(ini.get("debug", "update_url")));
    c.debugSelfUpdate = parseIntIn(ini.get("debug", "self_update"), 0, 1, 0) == 1;
    c.debugIgnoreAhbprot = parseIntIn(ini.get("debug", "ignore_ahbprot"), 0, 1, 0) == 1;
    c.debugWiiDryRun = parseIntIn(ini.get("debug", "wii_dry_run"), 0, 1, 0) == 1;
    c.debugOpenScene = std::string(trim(ini.get("debug", "open_scene")));
    c.debugBannerFile = std::string(trim(ini.get("debug", "banner_file")));
    c.debugDetailsBackMs = parseIntIn(ini.get("debug", "details_back_ms"), 0, 60000, 0);
    c.debugOptionSet = std::string(trim(ini.get("debug", "option_set")));
    c.debugTlsCa = std::string(trim(ini.get("debug", "tls_ca")));
    c.debugWizard = std::string(trim(ini.get("debug", "wizard")));
    c.debugFilter = std::string(trim(ini.get("debug", "filter")));
    c.debugPinInput = std::string(trim(ini.get("debug", "pin_input")));
    c.debugFakeDisc = std::string(trim(ini.get("debug", "fake_disc")));
}

Config configFromIni(const IniDoc& ini, bool tls) {
    Config c;
    const std::string_view lang = ini.get("general", "language");
    if (lang == "de") c.language = Config::Language::De;
    else if (lang == "en") c.language = Config::Language::En;

    if (ini.get("general", "library") == "sd") c.library = Config::LibraryDevice::Sd;
    c.libraryRules = libraryRulesFromIni(ini);
    c.sortOrder = parseSortOrder(ini.get("library", "sort")).value_or(SortOrder::Title);
    c.usbTimeoutSec = parseIntIn(ini.get("general", "usb_timeout"), 1, 120, c.usbTimeoutSec);

    const std::string_view mode = ini.get("theme", "mode");
    if (mode == "dark") c.themeMode = ThemeMode::Dark;
    else if (mode == "auto") c.themeMode = ThemeMode::Auto;
    c.darkFromMin = parseClock(ini.get("theme", "dark_from"), c.darkFromMin);
    c.darkToMin = parseClock(ini.get("theme", "dark_to"), c.darkToMin);
    // Spec v2 §16: [theme] name; pack ist der Schlüssel aus M16 und gilt nur, wenn name fehlt.
    std::string_view pack = trim(ini.get("theme", "name"));
    if (pack.empty()) pack = trim(ini.get("theme", "pack"));
    if (isThemePackName(pack)) c.themePack = std::string(pack);

    c.music = parseIntIn(ini.get("audio", "music"), 0, 1, 1) == 1;
    c.musicVolume = parseIntIn(ini.get("audio", "music_volume"), 0, 100, c.musicVolume);
    c.sounds = parseIntIn(ini.get("audio", "sounds"), 0, 1, 1) == 1;
    c.banners = parseIntIn(ini.get("ui", "banners"), 0, 1, 1) == 1;

    const std::string_view region = ini.get("covers", "region");
    for (std::string_view allowed : {"auto", "EN", "DE", "FR", "US", "JA"}) {
        if (region == allowed) c.coverRegion = std::string(region);
    }
    c.coverAuto = parseIntIn(ini.get("covers", "auto"), 0, 1, 1) == 1;
    const std::string_view repo = trim(ini.get("emulators", "repo"));
    c.emulatorRepo = sourceForBuild(repo, kDefaultEmuRepoHttp, kDefaultEmuRepoHttps, defaultEmuRepo(tls));
    const std::string_view coverBase = trim(ini.get("covers", "base"));
    c.coverBase = sourceForBuild(coverBase, kGametdbCoverBaseHttp, kGametdbCoverBaseHttps, defaultCoverBase(tls));
    const std::string_view cheatSource = trim(ini.get("cheats", "source"));
    const bool cheatSourceOk = cheatSource.starts_with("https://") || cheatSource.starts_with("http://");
    c.cheatSource = std::string(cheatSourceOk ? cheatSource : defaultCheatSource(tls));
    c.parental = parentalFromIni(ini);
    c.emuNandPath = std::string(trim(ini.get("emunand", "path")));
    const std::string_view ios = ini.get("wii", "ios");
    c.wiiIos = parseIosValue(ios);
    if (isBadIosValue(ios)) c.badKeys.emplace_back("wii.ios");
    applyDebugIni(c, ini);
    return c;
}

std::string formatClock(int minuteOfDay) {
    const int m = std::clamp(minuteOfDay, 0, 24 * 60 - 1);
    std::string out = "00:00";
    out[0] = static_cast<char>('0' + m / 60 / 10);
    out[1] = static_cast<char>('0' + m / 60 % 10);
    out[3] = static_cast<char>('0' + m % 60 / 10);
    out[4] = static_cast<char>('0' + m % 60 % 10);
    return out;
}

namespace {

struct IniValue {
    const char* section;
    std::string key;
    std::string value;
    bool written = false;
};

std::vector<IniValue> configValues(const Config& c) {
    const char* lang = c.language == Config::Language::De ? "de" : c.language == Config::Language::En ? "en" : "auto";
    const char* mode = c.themeMode == ThemeMode::Dark ? "dark" : c.themeMode == ThemeMode::Auto ? "auto" : "light";
    std::vector<IniValue> v = {
        {"general", "language", lang},
        {"general", "library", c.library == Config::LibraryDevice::Sd ? "sd" : "usb"},
        {"general", "usb_timeout", std::to_string(c.usbTimeoutSec)},
        {"library", "sources", sourcesToText(c.libraryRules.sources)},
        {"library", "sort", sortOrderName(c.sortOrder)},
        {"theme", "mode", mode},
        {"theme", "dark_from", formatClock(c.darkFromMin)},
        {"theme", "dark_to", formatClock(c.darkToMin)},
        {"theme", "name", c.themePack},
        {"audio", "music", c.music ? "1" : "0"},
        {"audio", "music_volume", std::to_string(std::clamp(c.musicVolume, 0, 100))},
        {"audio", "sounds", c.sounds ? "1" : "0"},
        {"ui", "banners", c.banners ? "1" : "0"},
        {"covers", "region", c.coverRegion},
        {"covers", "auto", c.coverAuto ? "1" : "0"},
        {"emulators", "repo", c.emulatorRepo},
        {"wii", "ios", c.wiiIos ? std::to_string(static_cast<unsigned>(*c.wiiIos)) : std::string("auto")},
    };
    for (auto& [key, value] : parentalIniValues(c.parental)) v.push_back({"parental", std::move(key), std::move(value)});
    if (!c.emuNandPath.empty()) v.push_back({"emunand", "path", c.emuNandPath});
    return v;
}

}  // namespace

std::string configToIniText(const Config& c, std::string_view previousText) {
    const char* eol = previousText.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<IniValue> values = configValues(c);

    // Ausgabezeilen ohne Zeilenende; je bekanntem Abschnitt die Stelle hinter seiner letzten Inhaltszeile.
    std::vector<std::string> out;
    std::vector<std::pair<std::string, std::size_t>> insertAt;
    std::string section;
    std::string_view text = previousText;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        const std::string_view line = trim(raw);
        const std::size_t eq = line.find('=');
        if (!line.empty() && line.front() == '[' && line.back() == ']' && eq == std::string_view::npos) {
            section = std::string(trim(line.substr(1, line.size() - 2)));
            out.emplace_back(raw);
            insertAt.emplace_back(section, out.size());
            continue;
        }
        if (eq != std::string_view::npos && line.front() != ';' && line.front() != '#') {
            const std::string_view key = trim(line.substr(0, eq));
            // Die Test-PIN steht im Klartext da und wird nie zurückgeschrieben; [theme] pack heißt jetzt name.
            if ((section == "debug" && key == "pin_input") || (section == "theme" && key == "pack") ||
                (section == "emunand" && key == "path" && c.emuNandPath.empty()) ||
                (section == "parental" && key == "lock_until" && c.parental.lockUntil == 0))
                continue;
            auto it = std::find_if(values.begin(), values.end(),
                                   [&](const IniValue& v) { return section == v.section && key == v.key; });
            if (it != values.end()) {
                if (it->written) continue;  // doppelter Schlüssel: nur der erste zählt (wie configFromIni)
                it->written = true;
                out.push_back(std::string(key) + " = " + it->value);
                insertAt.back().second = out.size();
                continue;
            }
        }
        out.emplace_back(raw);
        if (!line.empty() && line.front() != ';' && line.front() != '#' && !insertAt.empty()) {
            insertAt.back().second = out.size();
        }
    }

    // Fehlende Schlüssel ans Ende des ersten passenden Abschnitts, von hinten nach vorn einfügen.
    for (auto at = insertAt.rbegin(); at != insertAt.rend(); ++at) {
        const bool firstOfName = std::find_if(insertAt.begin(), insertAt.end(), [&](const auto& p) {
                                     return p.first == at->first;
                                 }) == at.base() - 1;
        if (!firstOfName) continue;
        std::vector<std::string> missing;
        for (IniValue& v : values) {
            if (!v.written && at->first == v.section) {
                v.written = true;
                missing.push_back(std::string(v.key) + " = " + v.value);
            }
        }
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(at->second), missing.begin(), missing.end());
    }
    for (IniValue& v : values) {
        if (v.written) continue;
        if (out.empty() || out.back() != std::string("[") + v.section + "]") out.push_back(std::string("[") + v.section + "]");
        for (IniValue& w : values) {
            if (!w.written && std::string_view(w.section) == v.section) {
                w.written = true;
                out.push_back(std::string(w.key) + " = " + w.value);
            }
        }
    }

    std::string result;
    for (const std::string& l : out) {
        result += l;
        result += eol;
    }
    return result;
}

std::string defaultConfigText(bool tls) {
    return "[general]\n"
           "language = auto\n"
           "library = usb\n"
           "usb_timeout = 15\n"
           "[library]\n"
           "sources = auto\n"
           "[theme]\n"
           "mode = light\n"
           "dark_from = 19:00\n"
           "dark_to = 07:00\n"
           "[audio]\n"
           "music = 1\n"
           "music_volume = 60\n"
           "sounds = 1\n"
           "[ui]\n"
           "banners = 1\n"
           "[covers]\n"
           "region = auto\n"
           "auto = 1\n"
           "[emulators]\n"
           "repo = " +
           std::string(defaultEmuRepo(tls)) + "\n" +
           "[wii]\n"
           "ios = auto\n";
}

}  // namespace nl
