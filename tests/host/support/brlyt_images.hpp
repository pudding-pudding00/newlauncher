#pragma once
// Baut BRLYT-Dateien für die Host-Tests (M18) aus eigenen Daten.

#include <array>
#include <bit>
#include <optional>
#include <string>
#include <vector>

#include "core/brlyt.hpp"
#include "support/disc_images.hpp"

namespace nl::test {

struct MatSpec {
    std::string name;
    std::vector<u16> texMaps;
    std::vector<std::array<float, 5>> srts;
    std::optional<Color> matColor;
};

struct PaneSpec {
    std::string kind = "pan1";  // pan1, pic1, txt1, bnd1
    std::string name;
    u8 flags = 0x03;            // Bit 0 sichtbar, Bit 1 Alpha wirkt auf Kinder
    u8 origin = 4;
    u8 alpha = 255;
    std::array<float, 10> f{0, 0, 0, 0, 0, 0, 1, 1, 0, 0};  // trans xyz, rot xyz, scale xy, Breite, Höhe
    u16 material = 0;
    std::array<Color, 4> vtx{Color{255, 255, 255, 255}, Color{255, 255, 255, 255}, Color{255, 255, 255, 255},
                             Color{255, 255, 255, 255}};
    std::vector<std::array<float, 8>> uvs;
    std::vector<PaneSpec> children;
};

namespace detail {
inline void be16(std::vector<u8>& b, u16 v) {
    b.push_back(static_cast<u8>(v >> 8));
    b.push_back(static_cast<u8>(v));
}
inline void be32(std::vector<u8>& b, u32 v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<u8>(v >> s));
}
inline void f32(std::vector<u8>& b, float v) { be32(b, std::bit_cast<u32>(v)); }
inline void name(std::vector<u8>& b, const std::string& s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b.push_back(i < s.size() ? static_cast<u8>(s[i]) : 0);
}
inline void color(std::vector<u8>& b, Color c) {
    b.push_back(c.r);
    b.push_back(c.g);
    b.push_back(c.b);
    b.push_back(c.a);
}

struct Sections {
    std::vector<u8> bytes;
    u16 count = 0;
    void add(const char* magic, const std::vector<u8>& body) {
        std::vector<u8> s(magic, magic + 4);
        be32(s, static_cast<u32>(8 + body.size()));
        s.insert(s.end(), body.begin(), body.end());
        while (s.size() % 4) s.push_back(0);
        putBe32(s, 4, static_cast<u32>(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
        ++count;
    }
};

inline std::vector<u8> paneBody(const PaneSpec& p) {
    std::vector<u8> b{p.flags, p.origin, p.alpha, 0};
    name(b, p.name, 16);
    name(b, "", 8);
    for (float v : p.f) f32(b, v);
    if (p.kind == "pic1") {
        for (const Color& c : p.vtx) color(b, c);
        be16(b, p.material);
        b.push_back(static_cast<u8>(p.uvs.size()));
        b.push_back(0);
        for (const auto& uv : p.uvs)
            for (float v : uv) f32(b, v);
    } else if (p.kind == "txt1") {
        b.resize(b.size() + 0x30, 0);  // Textfelder, hier ohne Bedeutung
    }
    return b;
}

inline void panes(Sections& s, const std::vector<PaneSpec>& list) {
    for (const PaneSpec& p : list) {
        s.add(p.kind.c_str(), paneBody(p));
        if (!p.children.empty()) {
            s.add("pas1", {});
            panes(s, p.children);
            s.add("pae1", {});
        }
    }
}
}  // namespace detail

inline std::vector<u8> buildBrlyt(float w, float h, const std::vector<std::string>& textures,
                                  const std::vector<MatSpec>& mats, const std::vector<PaneSpec>& roots) {
    using namespace detail;
    Sections s;
    {
        std::vector<u8> b{1, 0, 0, 0};
        f32(b, w);
        f32(b, h);
        s.add("lyt1", b);
    }
    if (!textures.empty()) {
        std::vector<u8> b;
        be16(b, static_cast<u16>(textures.size()));
        be16(b, 0);
        std::string names;
        for (const std::string& t : textures) {
            be32(b, static_cast<u32>(textures.size() * 8 + names.size()));
            be32(b, 0);
            names += t;
            names += '\0';
        }
        b.insert(b.end(), names.begin(), names.end());
        s.add("txl1", b);
    }
    if (!mats.empty()) {
        std::vector<u8> b;
        be16(b, static_cast<u16>(mats.size()));
        be16(b, 0);
        const std::size_t table = b.size();
        b.resize(b.size() + mats.size() * 4, 0);
        for (std::size_t i = 0; i < mats.size(); ++i) {
            const MatSpec& m = mats[i];
            putBe32(b, table + i * 4, static_cast<u32>(8 + b.size()));  // relativ zum Abschnittsanfang
            name(b, m.name, 20);
            for (int k = 0; k < 12; ++k) be16(b, 0);  // drei s16-Farben
            b.resize(b.size() + 16, 0);              // TEV-Farben
            const u32 flags = static_cast<u32>(m.texMaps.size()) | static_cast<u32>(m.srts.size()) << 4 |
                              (m.matColor ? 1u << 27 : 0u);
            be32(b, flags);
            for (u16 t : m.texMaps) {
                be16(b, t);
                be16(b, 0);
            }
            for (const auto& srt : m.srts)
                for (float v : srt) f32(b, v);
            if (m.matColor) color(b, *m.matColor);
        }
        s.add("mat1", b);
    }
    panes(s, roots);
    std::vector<u8> out{'R', 'L', 'Y', 'T', 0xFE, 0xFF, 0x00, 0x0A};
    be32(out, static_cast<u32>(0x10 + s.bytes.size()));
    be16(out, 0x10);
    be16(out, s.count);
    out.insert(out.end(), s.bytes.begin(), s.bytes.end());
    return out;
}

// Pane mit Translation (x, y), Skalierung s und Größe w×h.
inline PaneSpec pane(std::string kind, std::string name, float x, float y, float w, float h, float s = 1) {
    PaneSpec p;
    p.kind = std::move(kind);
    p.name = std::move(name);
    p.f = {x, y, 0, 0, 0, 0, s, s, w, h};
    return p;
}

}  // namespace nl::test
