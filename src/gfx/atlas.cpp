#include "gfx/atlas.hpp"

namespace nl::gfx {

namespace {
constexpr u32 kPad = 1;
}

bool ShelfPacker::place(u16 w, u16 h, u16& outX, u16& outY) {
    if (w == 0 || h == 0 || w > width_ || h > height_) return false;
    if (cursorX_ + w > width_) {
        shelfY_ += shelfH_ + kPad;
        cursorX_ = 0;
        shelfH_ = 0;
    }
    if (shelfY_ + h > height_) return false;
    outX = static_cast<u16>(cursorX_);
    outY = static_cast<u16>(shelfY_);
    cursorX_ += w + kPad;
    if (h > shelfH_) shelfH_ = h;
    return true;
}

void rasterCircle(u8* dst, u16 stride, u16 x, u16 y, u16 diameter) {
    const float r = static_cast<float>(diameter) * 0.5f;
    const float r2 = r * r;
    constexpr int kSub = 4;
    for (int py = 0; py < diameter; ++py) {
        for (int px = 0; px < diameter; ++px) {
            int hits = 0;
            for (int sy = 0; sy < kSub; ++sy) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float dx = static_cast<float>(px) + (static_cast<float>(sx) + 0.5f) / kSub - r;
                    const float dy = static_cast<float>(py) + (static_cast<float>(sy) + 0.5f) / kSub - r;
                    if (dx * dx + dy * dy <= r2) ++hits;
                }
            }
            dst[static_cast<u32>(y + py) * stride + x + px] =
                static_cast<u8>((hits * 255 + kSub * kSub / 2) / (kSub * kSub));
        }
    }
}

void linearToI8(const u8* src, u16 w, u16 h, u8* dst) {
    u32 out = 0;
    for (u32 ty = 0; ty < h; ty += 4) {
        for (u32 tx = 0; tx < w; tx += 8) {
            for (u32 row = 0; row < 4; ++row) {
                const u8* line = src + (ty + row) * w + tx;
                for (u32 col = 0; col < 8; ++col) dst[out++] = line[col];
            }
        }
    }
}

}  // namespace nl::gfx
