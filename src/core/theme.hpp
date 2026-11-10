#pragma once
// Theme-Tokens hell/dunkel/auto (Spec 3.6).

#include "core/color.hpp"
#include "core/easing.hpp"
#include "core/types.hpp"

namespace nl {

enum class ThemeMode : u8 { Light, Dark, Auto };

struct Theme {
    Color bg, bar, barBorder, tile, tileFocus, text, text2, textMuted, accent, onAccent,
        accentSoft, accentSoftText, overlayDim, danger;  // danger: schwacher Akku
};

const Theme& lightTheme();
const Theme& darkTheme();
Theme lerp(const Theme& a, const Theme& b, float t);

// Auto: dunkel im Bereich [from, to), auch über Mitternacht; from == to heißt nie dunkel.
bool isDarkAt(ThemeMode mode, int minuteOfDay, int darkFromMin, int darkToMin);
ThemeMode nextMode(ThemeMode m);
const char* themeModeName(ThemeMode m);

inline constexpr float kThemeTransitionMs = 200.0f;

class ThemeState {
public:
    void setTarget(bool dark, bool animate);
    void update(float dtMs);
    const Theme& current() const { return current_; }
    bool dark() const { return dark_; }

private:
    void recompute();

    bool dark_ = false;
    Tween mix_;  // 0 = hell, 1 = dunkel
    Theme current_ = lightTheme();
};

}  // namespace nl
