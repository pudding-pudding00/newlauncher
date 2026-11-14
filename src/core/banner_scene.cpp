#include "core/banner_scene.hpp"

#include <cmath>
#include <numbers>

namespace nl {

namespace {
// Affine 2×3-Matrix: x' = a·x + b·y + tx, y' = c·x + d·y + ty.
struct Mtx {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Vec2 apply(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
};
Mtx mul(const Mtx& m, const Mtx& n) {  // m · n
    return {m.a * n.a + m.b * n.c, m.a * n.b + m.b * n.d, m.c * n.a + m.d * n.c, m.c * n.b + m.d * n.d,
            m.a * n.tx + m.b * n.ty + m.tx, m.c * n.tx + m.d * n.ty + m.ty};
}
// Pane-Matrix: erst skalieren, dann um z drehen (Grad, gegen den Uhrzeigersinn), dann verschieben.
Mtx local(const PaneState& s) {
    const float r = s.rot.z * std::numbers::pi_v<float> / 180.0f;
    const float cs = std::cos(r), sn = std::sin(r);
    return {cs * s.scale.x, -sn * s.scale.y, sn * s.scale.x, cs * s.scale.y, s.trans.x, s.trans.y};
}
u8 mul8(u32 x, u32 y) { return static_cast<u8>(x * y / 255); }

// Textur-SRT um die Texturmitte: skalieren, drehen, dann verschieben.
Vec2 srt(Vec2 uv, const std::array<float, 5>& t) {
    const float r = t[2] * std::numbers::pi_v<float> / 180.0f;
    const float cs = std::cos(r), sn = std::sin(r);
    const float x = (uv.x - 0.5f) * t[3], y = (uv.y - 0.5f) * t[4];
    return {cs * x - sn * y + 0.5f + t[0], sn * x + cs * y + 0.5f + t[1]};
}
}  // namespace

std::vector<PaneState> initialState(const Layout& l) {
    std::vector<PaneState> out;
    out.reserve(l.panes.size());
    for (const LytPane& p : l.panes) {
        PaneState s;
        s.trans = p.trans;
        s.rot = p.rot;
        s.scale = p.scale;
        s.size = p.size;
        s.alpha = p.alpha;
        s.visible = p.visible;
        s.vtxColor = p.vtxColor;
        if (p.material >= 0 && static_cast<std::size_t>(p.material) < l.materials.size()) {
            const LytMaterial& m = l.materials[static_cast<std::size_t>(p.material)];
            s.texSrt = m.texSrt;
            s.texIndex = m.texIndex;
            s.matColor = m.color;
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Quad> buildQuads(const Layout& l, std::span<const PaneState> state) {
    std::vector<Quad> out;
    if (state.size() != l.panes.size()) return out;
    const std::size_t n = l.panes.size();
    std::vector<Mtx> world(n);
    std::vector<u8> alpha(n);    // wirksames Alpha des Panes
    std::vector<bool> shown(n);  // selbst und alle Vorfahren sichtbar
    for (std::size_t i = 0; i < n; ++i) {
        const LytPane& p = l.panes[i];
        const PaneState& s = state[i];
        const s32 par = p.parent;
        const bool hasParent = par >= 0 && static_cast<std::size_t>(par) < i;
        const std::size_t pi = hasParent ? static_cast<std::size_t>(par) : 0;
        world[i] = hasParent ? mul(world[pi], local(s)) : local(s);
        const bool inherit = hasParent && l.panes[pi].influencedAlpha;
        alpha[i] = inherit ? mul8(s.alpha, alpha[pi]) : s.alpha;
        shown[i] = s.visible && (!hasParent || shown[pi]);
        if (!shown[i] || !p.isPicture || p.material < 0) continue;
        if (s.texIndex >= 0 && static_cast<std::size_t>(s.texIndex) >= l.textures.size()) continue;

        // Ecken je Ursprung: horizontal links/Mitte/rechts, vertikal oben/Mitte/unten (+Y oben).
        const float w = s.size.x, h = s.size.y;
        const float left = -w * static_cast<float>(p.origin % 3) / 2.0f;
        const float top = h * static_cast<float>(p.origin / 3) / 2.0f;
        const Vec2 corner[4] = {{left, top}, {left + w, top}, {left, top - h}, {left + w, top - h}};
        Quad q;
        q.texIndex = s.texIndex;
        for (std::size_t k = 0; k < 4; ++k) {
            q.pos[k] = world[i].apply(corner[k]);
            q.uv[k] = srt(p.uv[k], s.texSrt);
            const Rgba v = s.vtxColor[k], m = s.matColor;
            q.color[k] = Rgba{mul8(v.r, m.r), mul8(v.g, m.g), mul8(v.b, m.b), mul8(mul8(v.a, m.a), alpha[i])};
        }
        out.push_back(q);
    }
    return out;
}

}  // namespace nl
