#include "gfx/text.hpp"

namespace nl::gfx {

namespace {
constexpr u32 kEllipsis = 0x2026;

float roundf(float v) { return static_cast<float>(static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f)); }

// Rastet die linke Glyphkante auf einen ganzen Bildpunkt ein: Der Atlas ist 1:1 zu Bildpunkten
// gerendert, so bleiben dünne Striche scharf statt zwischen zwei Pixeln zu verblassen.
void drawGlyph(DrawList& dl, const FontFace& f, const Glyph& g, float penX, float baselineY, Color c) {
    if (g.w <= 0.0f || g.h <= 0.0f) return;
    float x = penX + g.bearingX;
    if (f.pixelScaleX > 0.0f) x = roundf(x * f.pixelScaleX) / f.pixelScaleX;
    dl.quad(0, Rect{x, roundf(baselineY - g.bearingY), g.w, g.h}, g.uv, c);
}

std::size_t trimEnd(std::string_view s, std::size_t start, std::size_t end) {
    while (end > start && s[end - 1] == ' ') --end;
    return end;
}
}  // namespace

float measureText(const FontFace& f, std::string_view s) {
    float w = 0.0f;
    for (std::size_t pos = 0; pos < s.size();) w += f.glyph(decodeUtf8(s, pos)).advance;
    return w;
}

void drawText(DrawList& dl, const FontFace& f, float x, float baselineY, std::string_view s, Color c) {
    for (std::size_t pos = 0; pos < s.size();) {
        const Glyph& g = f.glyph(decodeUtf8(s, pos));
        drawGlyph(dl, f, g, x, baselineY, c);
        x += g.advance;
    }
}

void drawTextEllipsis(DrawList& dl, const FontFace& f, float x, float baselineY, float maxWidth,
                      std::string_view s, Color c) {
    if (s.empty()) return;
    if (measureText(f, s) <= maxWidth) {
        drawText(dl, f, x, baselineY, s, c);
        return;
    }
    const Glyph& dots = f.glyph(kEllipsis);
    if (maxWidth < dots.advance) return;
    const float budget = maxWidth - dots.advance;
    float pen = x;
    for (std::size_t pos = 0; pos < s.size();) {
        const Glyph& g = f.glyph(decodeUtf8(s, pos));
        if (pen - x + g.advance > budget) break;
        drawGlyph(dl, f, g, pen, baselineY, c);
        pen += g.advance;
    }
    drawGlyph(dl, f, dots, pen, baselineY, c);
}

int wrapText(const FontFace& f, std::string_view s, float maxWidth, std::span<Line> out) {
    int count = 0;
    std::size_t pos = 0;
    const std::size_t size = s.size();
    while (count < static_cast<int>(out.size())) {
        while (pos < size && s[pos] == ' ') ++pos;
        if (pos >= size) break;
        const std::size_t start = pos;
        std::size_t end = size, next = size;

        if (count + 1 < static_cast<int>(out.size())) {
            constexpr std::size_t kNone = static_cast<std::size_t>(-1);
            std::size_t breakEnd = kNone, breakNext = kNone;
            float w = 0.0f;
            std::size_t p = start;
            while (p < size) {
                std::size_t q = p;
                const u32 cp = decodeUtf8(s, q);
                const float adv = f.glyph(cp).advance;
                if (w + adv > maxWidth && p > start) {
                    if (cp == ' ') {
                        end = p; next = q;
                    } else if (breakEnd != kNone) {
                        end = breakEnd; next = breakNext;
                    } else {
                        end = p; next = p;  // hart umbrechen, nie mitten in einer UTF-8-Folge
                    }
                    break;
                }
                if (cp == ' ') {
                    breakEnd = p;
                    breakNext = q;
                }
                w += adv;
                p = q;
            }
        }
        end = trimEnd(s, start, end);
        out[count++] = Line{static_cast<u16>(start), static_cast<u16>(end - start)};
        pos = next;
    }
    return count;
}

}  // namespace nl::gfx
