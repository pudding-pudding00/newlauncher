#include "core/theme_file.hpp"

#include <algorithm>

namespace nl {

const char* const kThemeTokenNames[kThemeTokenCount] = {
    "bg",     "bar",       "bar_border",  "tile",             "tile_focus",  "text",  "text2",
    "text_muted", "accent", "on_accent", "accent_soft", "accent_soft_text", "overlay_dim", "danger",
};

namespace {
constexpr Color Theme::* kFields[kThemeTokenCount] = {
    &Theme::bg,     &Theme::bar,      &Theme::barBorder,  &Theme::tile,           &Theme::tileFocus,
    &Theme::text,   &Theme::text2,    &Theme::textMuted,  &Theme::accent,         &Theme::onAccent,
    &Theme::accentSoft, &Theme::accentSoftText, &Theme::overlayDim, &Theme::danger,
};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void readSection(const IniDoc& ini, std::string_view section, Theme& t, std::vector<std::string>* bad) {
    for (std::size_t i = 0; i < kThemeTokenCount; ++i) {
        const std::string_view v = ini.get(section, kThemeTokenNames[i]);
        if (v.empty()) continue;
        if (auto c = parseHexColor(v)) {
            t.*kFields[i] = *c;
        } else if (bad != nullptr) {
            bad->push_back(std::string(section) + "." + kThemeTokenNames[i]);
        }
    }
}
}  // namespace

Color& themeToken(Theme& t, std::size_t i) { return t.*kFields[i]; }
const Color& themeToken(const Theme& t, std::size_t i) { return t.*kFields[i]; }

std::optional<Color> parseHexColor(std::string_view s) {
    s = trim(s);
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return std::nullopt;
    u8 v[4] = {0, 0, 0, 0xFF};
    for (std::size_t i = 0; i * 2 + 1 < s.size(); ++i) {
        const int hi = hexDigit(s[1 + i * 2]), lo = hexDigit(s[2 + i * 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        v[i] = static_cast<u8>(hi * 16 + lo);
    }
    return Color{v[0], v[1], v[2], v[3]};
}

ThemePair themeFromIni(const IniDoc& ini, std::vector<std::string>* bad) {
    ThemePair p;
    readSection(ini, "light", p.light, bad);
    readSection(ini, "dark", p.dark, bad);
    return p;
}

bool isThemePackName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    return name.find_first_of("/\\:") == std::string_view::npos && name.find("..") == std::string_view::npos;
}

std::vector<std::string> listThemePacks(FileSystem& fs, std::string_view dataDir) {
    std::vector<std::string> out;
    const std::string root = joinPath(dataDir, "themes");
    auto entries = fs.list(root);
    if (!entries.isOk()) return out;
    for (const DirEntry& e : entries.value()) {
        if (!e.isDir || !isThemePackName(e.name)) continue;
        u8 probe = 0;
        if (!fs.read(joinPath(joinPath(root, e.name), "theme.ini"), 0, std::span<u8>(&probe, 1)).isOk()) continue;
        out.push_back(e.name);
    }
    std::sort(out.begin(), out.end());
    if (out.size() > kMaxThemePacks) out.resize(kMaxThemePacks);
    return out;
}

}  // namespace nl
