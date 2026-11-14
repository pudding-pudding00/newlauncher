#include "core/brlyt.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>

namespace nl {

namespace {
u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}
u16 be16(std::span<const u8> b, std::size_t at) { return static_cast<u16>(b[at] << 8 | b[at + 1]); }
// Nicht endliche Werte (NaN, ±∞) werden 0, damit keine Folgerechnung explodiert.
float f32(std::span<const u8> b, std::size_t at) {
    const float v = std::bit_cast<float>(be32(b, at));
    return std::isfinite(v) ? v : 0.0f;
}
Rgba rgba(std::span<const u8> b, std::size_t at) { return Rgba{b[at], b[at + 1], b[at + 2], b[at + 3]}; }
// Feste Namensfelder sind nicht immer nullterminiert.
std::string fixedName(std::span<const u8> b, std::size_t at, std::size_t n) {
    std::size_t len = 0;
    while (len < n && b[at + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(&b[at]), len);
}
Result<Layout> invalid(const char* what) { return Result<Layout>::err(Error{ErrorCode::InvalidData, what}); }

bool is(std::span<const u8> s, const char* magic) { return std::memcmp(s.data(), magic, 4) == 0; }

// Abschnitt lyt1: u8 zentriert, 3 Byte Füllung, Breite, Höhe.
bool readLyt1(std::span<const u8> s, Layout& l) {
    if (s.size() < 0x14) return false;
    l.width = f32(s, 0x0C);
    l.height = f32(s, 0x10);
    // 0, negativ, NaN oder unendlich: keine sinnvolle Fläche → ungültiges Banner (Cover statt leerer Fläche).
    return std::isfinite(l.width) && std::isfinite(l.height) && l.width > 0 && l.height > 0;
}

// Abschnitt txl1: u16 Anzahl, Füllung, dann je Textur {u32 Namensversatz ab Tabellenanfang (+0x0C), u32 0}.
bool readTxl1(std::span<const u8> s, Layout& l) {
    if (s.size() < 0x0C) return false;
    const u32 count = be16(s, 8);
    if (count > kLytMaxTextures || (s.size() - 0x0C) / 8 < count) return false;
    for (u32 i = 0; i < count; ++i) {
        const std::size_t at = 0x0C + std::size_t{be32(s, 0x0C + std::size_t{i} * 8)};
        if (at >= s.size()) return false;
        const std::size_t avail = std::min<std::size_t>(s.size() - at, kLytNameMax + 1);
        const std::size_t len = std::string_view(reinterpret_cast<const char*>(&s[at]), avail).find('\0');
        if (len == std::string_view::npos) return false;  // ohne Ende oder länger als kLytNameMax
        l.textures.emplace_back(reinterpret_cast<const char*>(&s[at]), len);
    }
    return true;
}

// Abschnitt mat1: u16 Anzahl, Füllung, dann u32-Versätze ab Abschnittsanfang. Ein Material: Name[20],
// drei s16[4]-Farben, vier u8[4]-TEV-Farben, u32 Flags bei 0x3C, Daten ab 0x40 in dieser Reihenfolge:
// Textur-Maps (Bits 0–3, je 4 Byte), Textur-SRTs (Bits 4–7, je 0x14), TexCoordGens (Bits 8–11, je 4),
// ChanCtrl (Bit 25, 4), Materialfarbe (Bit 27, RGBA).
bool readMat1(std::span<const u8> s, Layout& l) {
    if (s.size() < 0x0C) return false;
    const u32 count = be16(s, 8);
    if (count > kLytMaxMaterials || (s.size() - 0x0C) / 4 < count) return false;
    for (u32 i = 0; i < count; ++i) {
        const std::size_t at = be32(s, 0x0C + std::size_t{i} * 4);
        if (at > s.size() || s.size() - at < 0x40) return false;
        LytMaterial m;
        m.name = fixedName(s, at, 20);
        const u32 flags = be32(s, at + 0x3C);
        const u32 maps = flags & 0xF, srts = (flags >> 4) & 0xF, gens = (flags >> 8) & 0xF;
        const bool chan = (flags >> 25) & 1, color = (flags >> 27) & 1;
        const std::size_t need = 0x40 + std::size_t{maps} * 4 + std::size_t{srts} * 0x14 + std::size_t{gens} * 4 +
                                 (chan ? 4 : 0) + (color ? 4 : 0);
        if (s.size() - at < need) return false;
        std::size_t p = at + 0x40;
        if (maps > 0) {
            const u16 idx = be16(s, p);
            m.texIndex = static_cast<s16>(idx > 0x7FFF ? 0x7FFF : idx);  // außerhalb bleibt außerhalb
        }
        p += std::size_t{maps} * 4;
        if (srts > 0)
            for (std::size_t k = 0; k < 5; ++k) m.texSrt[k] = f32(s, p + k * 4);
        p += std::size_t{srts} * 0x14 + std::size_t{gens} * 4 + (chan ? 4 : 0);
        if (color) m.color = rgba(s, p);
        l.materials.push_back(std::move(m));
    }
    return true;
}

// Gemeinsamer Pane-Kopf (0x4C mit Abschnittskopf): Flags (Bit 0 sichtbar, Bit 1 Alpha wirkt auf Kinder),
// Ursprung, Alpha, Füllung, Name[16], Benutzerdaten[8], dann trans xyz, rot xyz, scale xy, Breite, Höhe.
bool readPane(std::span<const u8> s, LytPane& p) {
    if (s.size() < 0x4C) return false;
    p.visible = (s[8] & 1) != 0;
    p.influencedAlpha = (s[8] & 2) != 0;
    p.origin = s[9] <= 8 ? s[9] : 4;
    p.alpha = s[10];
    p.name = fixedName(s, 0x0C, 16);
    p.trans = {f32(s, 0x24), f32(s, 0x28), f32(s, 0x2C)};
    p.rot = {f32(s, 0x30), f32(s, 0x34), f32(s, 0x38)};
    p.scale = {f32(s, 0x3C), f32(s, 0x40)};
    p.size = {f32(s, 0x44), f32(s, 0x48)};
    return true;
}

// pic1: Pane-Kopf, vier Vertexfarben (oben links, oben rechts, unten links, unten rechts), u16 Material,
// u8 UV-Satzanzahl, Füllung, dann je Satz vier (u, v)-Paare. Gelesen wird der erste Satz.
bool readPic1(std::span<const u8> s, const Layout& l, LytPane& p) {
    if (!readPane(s, p) || s.size() < 0x60) return false;
    p.isPicture = true;
    for (std::size_t k = 0; k < 4; ++k) p.vtxColor[k] = rgba(s, 0x4C + k * 4);
    const u16 mat = be16(s, 0x5C);
    p.material = mat < l.materials.size() ? static_cast<s32>(mat) : -1;
    const u8 uvSets = s[0x5E];
    if ((s.size() - 0x60) / 0x20 < uvSets) return false;
    if (uvSets > 0)
        for (std::size_t k = 0; k < 4; ++k) p.uv[k] = {f32(s, 0x60 + k * 8), f32(s, 0x64 + k * 8)};
    return true;
}
}  // namespace

Result<Layout> parseBrlyt(std::span<const u8> file) {
    if (file.size() < 0x10 || !is(file, "RLYT") || be16(file, 4) != 0xFEFF) return invalid("BRLYT: kein RLYT");
    if (be32(file, 8) > file.size()) return invalid("BRLYT: Datei abgeschnitten");
    const std::span<const u8> data = file.first(be32(file, 8));
    std::size_t at = be16(file, 0x0C);
    const u32 sections = be16(file, 0x0E);
    Layout l;
    bool haveLyt = false;
    std::vector<s32> stack;  // Eltern-Panes der offenen pas1-Ebenen
    s32 last = -1;           // zuletzt gelesenes Pane, Ziel des nächsten pas1
    for (u32 i = 0; i < sections; ++i) {
        if (at > data.size() || data.size() - at < 8) return invalid("BRLYT: Abschnitt hinter dem Ende");
        const u32 size = be32(data, at + 4);
        if (size < 8 || size > data.size() - at) return invalid("BRLYT: Abschnittslänge");
        const std::span<const u8> s = data.subspan(at, size);
        at += size;
        if (is(s, "lyt1")) {
            if (!readLyt1(s, l)) return invalid("BRLYT: lyt1");
            haveLyt = true;
        } else if (is(s, "txl1")) {
            if (!readTxl1(s, l)) return invalid("BRLYT: txl1");
        } else if (is(s, "mat1")) {
            if (!readMat1(s, l)) return invalid("BRLYT: mat1");
        } else if (is(s, "pan1") || is(s, "pic1") || is(s, "txt1") || is(s, "wnd1") || is(s, "bnd1")) {
            if (l.panes.size() >= kLytMaxPanes) return invalid("BRLYT: zu viele Panes");
            LytPane p;
            const bool ok = is(s, "pic1") ? readPic1(s, l, p) : readPane(s, p);
            if (!ok) return invalid("BRLYT: Pane");
            if (!is(s, "pan1") && !is(s, "pic1")) ++l.skipped;  // Text, Fenster, Begrenzung: nur Gruppe
            p.parent = stack.empty() ? -1 : stack.back();
            last = static_cast<s32>(l.panes.size());
            l.panes.push_back(std::move(p));
        } else if (is(s, "pas1")) {
            if (last < 0 || stack.size() >= kLytMaxPanes) return invalid("BRLYT: pas1 ohne Pane");
            stack.push_back(last);
        } else if (is(s, "pae1")) {
            if (stack.empty()) return invalid("BRLYT: pae1 ohne pas1");
            last = stack.back();
            stack.pop_back();
        } else {
            ++l.skipped;  // grp1, grs1, gre1, fnl1, usd1 …: für das Bild ohne Bedeutung
        }
    }
    if (!haveLyt) return invalid("BRLYT: lyt1 fehlt");
    return Result<Layout>::ok(std::move(l));
}

}  // namespace nl
