#include "ui/qr_view.hpp"

#include <cmath>

namespace nl::ui {

void drawQr(gfx::DrawList& dl, const gfx::AtlasShapes& shapes, const QrCode& qr, gfx::Rect area) {
    if (qr.size <= 0) return;
    const int total = qr.size + 8;  // 4 Module Ruhezone je Seite
    const float side = area.w < area.h ? area.w : area.h;
    // Ganze Pixel je Modul, damit Scanner saubere Kanten sehen; bei sehr kleiner Fläche notfalls Bruchteile.
    float m = std::floor(side / static_cast<float>(total));
    if (m < 1.0f) m = side / static_cast<float>(total);
    const float px = m * static_cast<float>(total);
    const float x0 = area.x + (area.w - px) / 2, y0 = area.y + (area.h - px) / 2;
    gfx::fillRect(dl, shapes, gfx::Rect{x0, y0, px, px}, Color{255, 255, 255, 255});
    const Color black{0, 0, 0, 255};
    for (int y = 0; y < qr.size; ++y) {
        int x = 0;
        while (x < qr.size) {
            if (!qr.dark(x, y)) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < qr.size && qr.dark(x, y)) ++x;
            gfx::fillRect(dl, shapes,
                          gfx::Rect{x0 + static_cast<float>(start + 4) * m, y0 + static_cast<float>(y + 4) * m,
                                    static_cast<float>(x - start) * m, m},
                          black);
        }
    }
}

}  // namespace nl::ui
