#pragma once
// Schnittstelle der UI zu den Covern (Spec 4.3): Szenen melden in update(), welche Cover sie brauchen,
// und fragen in draw() Textur und Einblend-Alpha ab. Auf der Wii steckt CoverService dahinter, in Tests ein Fake.

#include "core/cover_cache.hpp"
#include "core/game.hpp"
#include "gfx/draw_list.hpp"

namespace nl::ui {

struct CoverView {
    gfx::TextureId texture = 0;  // 0 = kein Cover, Platzhalter zeichnen
    float alpha = 0.0f;
};

class CoverProvider {
public:
    virtual void want(const Game& game, CoverPriority priority) = 0;
    virtual CoverView view(const Game& game) const = 0;

protected:
    ~CoverProvider() = default;
};

// Cover (5:7) zentriert in r eingepasst, mit inset Rand.
inline gfx::Rect fitCoverRect(gfx::Rect r, float inset) {
    const float w = r.w - 2 * inset, h = r.h - 2 * inset;
    if (w <= 0 || h <= 0) return gfx::Rect{r.x + r.w / 2, r.y + r.h / 2, 0, 0};
    const float fw = w * 7.0f <= h * 5.0f ? w : h * 5.0f / 7.0f;
    const float fh = fw * 7.0f / 5.0f;
    return gfx::Rect{r.x + (r.w - fw) / 2, r.y + (r.h - fh) / 2, fw, fh};
}

}  // namespace nl::ui
