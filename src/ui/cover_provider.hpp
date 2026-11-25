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
    gfx::UvRect uv{0, 0, 1, 1};   // M23: Inhalt ohne transparenten Rand
    float aspect = 5.0f / 7.0f;   // Breite/Höhe des Inhalts
};

class CoverProvider {
public:
    virtual void want(const Game& game, CoverPriority priority) = 0;
    virtual CoverView view(const Game& game) const = 0;

protected:
    ~CoverProvider() = default;
};

// Rechteck mit Seitenverhältnis aspect (Breite/Höhe) zentriert in r eingepasst, mit inset Rand.
inline gfx::Rect fitAspectRect(gfx::Rect r, float inset, float aspect) {
    const float w = r.w - 2 * inset, h = r.h - 2 * inset;
    if (w <= 0 || h <= 0 || aspect <= 0) return gfx::Rect{r.x + r.w / 2, r.y + r.h / 2, 0, 0};
    const float fw = w <= h * aspect ? w : h * aspect;
    const float fh = fw / aspect;
    return gfx::Rect{r.x + (r.w - fw) / 2, r.y + (r.h - fh) / 2, fw, fh};
}
inline gfx::Rect fitCoverRect(gfx::Rect r, float inset) { return fitAspectRect(r, inset, 5.0f / 7.0f); }

}  // namespace nl::ui
