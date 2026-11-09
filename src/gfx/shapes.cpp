#include "gfx/shapes.hpp"

namespace nl::gfx {

void fillRect(DrawList& dl, const AtlasShapes& s, Rect r, Color c) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    dl.quad(0, r, s.white, c);
}

void roundRect(DrawList& dl, const AtlasShapes& s, Rect r, float radius, Color c) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float maxR = (r.w < r.h ? r.w : r.h) * 0.5f;
    if (radius > maxR) radius = maxR;
    if (radius <= 0.0f) {
        dl.quad(0, r, s.white, c);
        return;
    }
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    const float midH = r.h - 2.0f * radius;
    dl.quad(0, Rect{x0 + radius, y0, r.w - 2.0f * radius, r.h}, s.white, c);
    dl.quad(0, Rect{x0, y0 + radius, radius, midH}, s.white, c);
    dl.quad(0, Rect{x1 - radius, y0 + radius, radius, midH}, s.white, c);

    const UvRect& k = s.circle;
    const float um = (k.u0 + k.u1) * 0.5f, vm = (k.v0 + k.v1) * 0.5f;
    dl.quad(0, Rect{x0, y0, radius, radius}, UvRect{k.u0, k.v0, um, vm}, c);
    dl.quad(0, Rect{x1 - radius, y0, radius, radius}, UvRect{um, k.v0, k.u1, vm}, c);
    dl.quad(0, Rect{x1 - radius, y1 - radius, radius, radius}, UvRect{um, vm, k.u1, k.v1}, c);
    dl.quad(0, Rect{x0, y1 - radius, radius, radius}, UvRect{k.u0, vm, um, k.v1}, c);
}

void circle(DrawList& dl, const AtlasShapes& s, float cx, float cy, float r, Color c) {
    if (r <= 0.0f) return;
    dl.quad(0, Rect{cx - r, cy - r, 2.0f * r, 2.0f * r}, s.circle, c);
}

}  // namespace nl::gfx
