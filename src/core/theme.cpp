#include "core/theme.hpp"

namespace nl {

namespace {
constexpr Color hex(u32 rgb) { return Color::fromHex(rgb); }

const Theme kLight{hex(0xEEF1F4), hex(0xFFFFFF), hex(0xD5DBE1), hex(0xFFFFFF), hex(0xFFFFFF),
                   hex(0x2B3540), hex(0x7A8794), hex(0x9AA5B0), hex(0x34BEED), hex(0xFFFFFF),
                   hex(0xE3F6FD), hex(0x0B6E91), Color::fromHex(0x000000, 0x73), hex(0xD93025)};

const Theme kDark{hex(0x121821), hex(0x1A2230), hex(0x2C3A4E), hex(0x243041), hex(0x3A4C66),
                  hex(0xFFFFFF), hex(0x8C9AAB), hex(0x4F5D6E), hex(0x34BEED), hex(0x0A1F2A),
                  hex(0x1E3A4A), hex(0x7FD6F5), Color::fromHex(0x000000, 0x99), hex(0xF28B82)};

u8 mix(u8 a, u8 b, float t) {
    const float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    return static_cast<u8>(v + 0.5f);
}

Color mix(Color a, Color b, float t) {
    return Color{mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), mix(a.a, b.a, t)};
}
}  // namespace

const Theme& lightTheme() { return kLight; }
const Theme& darkTheme() { return kDark; }

Theme lerp(const Theme& a, const Theme& b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return Theme{mix(a.bg, b.bg, t),         mix(a.bar, b.bar, t),
                 mix(a.barBorder, b.barBorder, t), mix(a.tile, b.tile, t),
                 mix(a.tileFocus, b.tileFocus, t), mix(a.text, b.text, t),
                 mix(a.text2, b.text2, t),   mix(a.textMuted, b.textMuted, t),
                 mix(a.accent, b.accent, t), mix(a.onAccent, b.onAccent, t),
                 mix(a.accentSoft, b.accentSoft, t), mix(a.accentSoftText, b.accentSoftText, t),
                 mix(a.overlayDim, b.overlayDim, t), mix(a.danger, b.danger, t)};
}

bool isDarkAt(ThemeMode mode, int minuteOfDay, int darkFromMin, int darkToMin) {
    if (mode == ThemeMode::Light) return false;
    if (mode == ThemeMode::Dark) return true;
    if (darkFromMin == darkToMin) return false;
    if (darkFromMin < darkToMin) return minuteOfDay >= darkFromMin && minuteOfDay < darkToMin;
    return minuteOfDay >= darkFromMin || minuteOfDay < darkToMin;
}

ThemeMode nextMode(ThemeMode m) {
    switch (m) {
        case ThemeMode::Light: return ThemeMode::Dark;
        case ThemeMode::Dark: return ThemeMode::Auto;
        case ThemeMode::Auto: break;
    }
    return ThemeMode::Light;
}

const char* themeModeName(ThemeMode m) {
    switch (m) {
        case ThemeMode::Light: return "Hell";
        case ThemeMode::Dark: return "Dunkel";
        case ThemeMode::Auto: break;
    }
    return "Automatisch";
}

void ThemeState::setTarget(bool dark, bool animate) {
    dark_ = dark;
    const float target = dark ? 1.0f : 0.0f;
    if (animate) {
        if (mix_.target() != target) mix_.to(target, kThemeTransitionMs);
    } else {
        mix_.snap(target);
    }
    recompute();
}

void ThemeState::update(float dtMs) {
    if (mix_.done()) return;
    mix_.update(dtMs);
    recompute();
}

void ThemeState::recompute() { current_ = lerp(kLight, kDark, mix_.value()); }

}  // namespace nl
