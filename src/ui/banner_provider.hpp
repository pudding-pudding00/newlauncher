#pragma once
// Schnittstelle der Detailansicht zum Banner (M18): die Szene meldet in update(), für welches Spiel sie ein
// Banner möchte, und fragt in draw() die fertige Textur ab. Auf der Wii steckt BannerService dahinter.

#include "core/game.hpp"
#include "gfx/draw_list.hpp"
#include "ui/banner_view.hpp"

namespace nl::ui {

struct BannerView {
    gfx::TextureId texture = 0;  // 0 = kein Banner (noch nicht geladen oder Rückfall aufs Cover)
    float width = 0.0f, height = 0.0f;  // Größe in Texeln (GC) bzw. Layout-Größe (Kanal)
    const BannerFrame* frame = nullptr;  // Kanal-Banner (M18 Task 5): Bild aus Quads statt einer Textur
};

class BannerProvider {
public:
    virtual void want(const Game& game) = 0;
    virtual BannerView view(const Game& game) const = 0;

protected:
    ~BannerProvider() = default;
};

}  // namespace nl::ui
