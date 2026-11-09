#pragma once
// Hilfen zum Aufbau des UI-Atlas (plattformfrei, host-getestet).

#include "core/types.hpp"

namespace nl::gfx {

// Zeilenweises Packen mit 1 px Abstand, damit lineare Filterung nicht in Nachbarn blutet.
class ShelfPacker {
public:
    ShelfPacker(u16 width, u16 height) : width_(width), height_(height) {}
    bool place(u16 w, u16 h, u16& outX, u16& outY);

private:
    u16 width_, height_;
    u32 cursorX_ = 0, shelfY_ = 0, shelfH_ = 0;
};

// Kreis mit Durchmesser `diameter` ab (x, y) in einen 8-Bit-Puffer; Rand per 4×4-Supersampling.
void rasterCircle(u8* dst, u16 stride, u16 x, u16 y, u16 diameter);

// Linearer 8-Bit-Puffer → GX-I8-Kachelformat (8×4 Pixel je Kachel). w % 8 == 0, h % 4 == 0.
void linearToI8(const u8* src, u16 w, u16 h, u8* dst);

}  // namespace nl::gfx
