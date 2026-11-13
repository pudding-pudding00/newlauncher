#include "core/brlan.hpp"

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
float f32(std::span<const u8> b, std::size_t at) {
    const float v = std::bit_cast<float>(be32(b, at));
    return std::isfinite(v) ? v : 0.0f;
}
bool is(std::span<const u8> s, const char* magic) { return std::memcmp(s.data(), magic, 4) == 0; }
Result<Anim> invalid(const char* what) { return Result<Anim>::err(Error{ErrorCode::InvalidData, what}); }
std::string fixedName(std::span<const u8> b, std::size_t at, std::size_t n) {
    std::size_t len = 0;
    while (len < n && b[at + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(&b[at]), len);
}
// Nullterminierter Name ab at innerhalb von s.
bool cName(std::span<const u8> s, std::size_t at, std::string& out) {
    if (at >= s.size()) return false;
    const std::size_t avail = std::min<std::size_t>(s.size() - at, kAnimNameMax + 1);
    const std::size_t len = std::string_view(reinterpret_cast<const char*>(&s[at]), avail).find('\0');
    if (len == std::string_view::npos) return false;  // ohne Ende oder länger als kAnimNameMax
    out.assign(reinterpret_cast<const char*>(&s[at]), len);
    return true;
}

// Ziel einer Spur je Tag-Typ und Ziel-Byte; false = nicht unterstützt (wird übersprungen).
bool targetOf(std::span<const u8> tag, u8 index, u8 target, AnimTarget& out) {
    if (is(tag, "RLPA")) {
        if (target > 9) return false;
        out = static_cast<AnimTarget>(target);
    } else if (is(tag, "RLVC")) {
        if (target > 0x10) return false;
        out = static_cast<AnimTarget>(static_cast<u8>(AnimTarget::VtxColor0) + target);
    } else if (is(tag, "RLVI")) {
        if (target != 0) return false;
        out = AnimTarget::Visible;
    } else if (is(tag, "RLTS")) {
        if (index != 0 || target > 4) return false;  // gezeichnet wird nur SRT 0
        out = static_cast<AnimTarget>(static_cast<u8>(AnimTarget::TexTx) + target);
    } else if (is(tag, "RLTP")) {
        if (index != 0 || target != 0) return false;
        out = AnimTarget::TexPattern;
    } else if (is(tag, "RLMC")) {
        if (target > 3) return false;  // TEV-Farben werden nicht gezeichnet
        out = static_cast<AnimTarget>(static_cast<u8>(AnimTarget::MatR) + target);
    } else {
        return false;
    }
    return true;
}

// Ein Info-Block: u8 Index, u8 Ziel, u8 Typ (1 Stufe, 2 Hermite), Füllung, u16 Keyanzahl, Füllung,
// u32 Versatz der Keys ab Blockanfang. Hermite-Key: f32 Bild, Wert, Steigung; Stufen-Key: f32 Bild, u16 Wert, Füllung.
bool readInfo(std::span<const u8> s, std::size_t at, std::span<const u8> tag, Track& t, bool& supported) {
    if (at > s.size() || s.size() - at < 0x0C) return false;
    const u8 type = s[at + 2];
    const u32 count = be16(s, at + 4);
    const std::size_t keys = at + std::size_t{be32(s, at + 8)};
    if (type != 1 && type != 2) return false;
    t.step = type == 1;
    const std::size_t keySize = t.step ? 8 : 12;
    if (count > kAnimMaxKeys || keys > s.size() || (s.size() - keys) / keySize < count) return false;
    supported = targetOf(tag, s[at], s[at + 1], t.target);
    if (!supported) return true;
    t.keys.reserve(count);
    for (u32 k = 0; k < count; ++k) {
        const std::size_t p = keys + std::size_t{k} * keySize;
        t.keys.push_back(t.step ? Key{f32(s, p), static_cast<float>(be16(s, p + 4)), 0}
                                : Key{f32(s, p), f32(s, p + 4), f32(s, p + 8)});
    }
    std::stable_sort(t.keys.begin(), t.keys.end(), [](const Key& a, const Key& b) { return a.frame < b.frame; });
    return true;
}

// Abschnitt pai1: u16 Bilder, u8 Schleife, Füllung, u16 Musteranzahl, u16 Einträge, u32 Versatz der
// Eintragstabelle ab Abschnittsanfang, ab 0x14 Mustertabelle (u32 ab Tabellenanfang) mit Namen.
// Eintrag: Name[20], u8 Tags, u8 Material, Füllung, u32-Tagversätze ab Eintrag. Tag: Magic, u8 Infos,
// Füllung, u32-Infoversätze ab Tag.
bool readPai1(std::span<const u8> s, Anim& a) {
    if (s.size() < 0x14) return false;
    a.frames = be16(s, 8);
    a.loop = s[0x0A] != 0;
    const u32 patterns = be16(s, 0x0C);
    const u32 entries = be16(s, 0x0E);
    const std::size_t table = be32(s, 0x10);
    if (patterns > kLytMaxTextures || (s.size() - 0x14) / 4 < patterns) return false;
    for (u32 i = 0; i < patterns; ++i) {
        std::string name;
        if (!cName(s, 0x14 + std::size_t{be32(s, 0x14 + std::size_t{i} * 4)}, name)) return false;
        a.patternTextures.push_back(std::move(name));
    }
    if (table > s.size() || (s.size() - table) / 4 < entries) return false;
    u32 totalKeys = 0;
    for (u32 e = 0; e < entries; ++e) {
        const std::size_t en = be32(s, table + std::size_t{e} * 4);
        if (en > s.size() || s.size() - en < 0x18) return false;
        const std::string name = fixedName(s, en, 20);
        const u32 tags = s[en + 0x14];
        const bool material = s[en + 0x15] != 0;
        if ((s.size() - en - 0x18) / 4 < tags) return false;
        for (u32 t = 0; t < tags; ++t) {
            const std::size_t tg = en + std::size_t{be32(s, en + 0x18 + std::size_t{t} * 4)};
            if (tg > s.size() || s.size() - tg < 8) return false;
            const std::span<const u8> tag = s.subspan(tg, 4);
            const u32 infos = s[tg + 4];
            if ((s.size() - tg - 8) / 4 < infos) return false;
            for (u32 i = 0; i < infos; ++i) {
                if (a.tracks.size() >= kAnimMaxTracks) return false;
                Track tr;
                tr.pane = name;
                tr.material = material;
                bool supported = false;
                if (!readInfo(s, tg + std::size_t{be32(s, tg + 8 + std::size_t{i} * 4)}, tag, tr, supported)) return false;
                if (!supported) {
                    ++a.skipped;
                    continue;
                }
                totalKeys += static_cast<u32>(tr.keys.size());
                if (totalKeys > kAnimMaxTotalKeys) return false;
                a.tracks.push_back(std::move(tr));
            }
        }
    }
    return true;
}

u8 channel(float v) { return static_cast<u8>(std::clamp(std::lround(v), 0L, 255L)); }

void applyMaterial(const Layout& l, const Anim& a, AnimTarget target, float v, PaneState& s) {
    switch (target) {
        case AnimTarget::TexTx: s.texSrt[0] = v; break;
        case AnimTarget::TexTy: s.texSrt[1] = v; break;
        case AnimTarget::TexRot: s.texSrt[2] = v; break;
        case AnimTarget::TexSx: s.texSrt[3] = v; break;
        case AnimTarget::TexSy: s.texSrt[4] = v; break;
        case AnimTarget::MatR: s.matColor.r = channel(v); break;
        case AnimTarget::MatG: s.matColor.g = channel(v); break;
        case AnimTarget::MatB: s.matColor.b = channel(v); break;
        case AnimTarget::MatA: s.matColor.a = channel(v); break;
        case AnimTarget::TexPattern: {
            const long i = std::lround(v);
            if (i < 0 || static_cast<std::size_t>(i) >= a.patternTextures.size()) break;
            const auto& name = a.patternTextures[static_cast<std::size_t>(i)];
            const auto it = std::find(l.textures.begin(), l.textures.end(), name);
            if (it != l.textures.end()) s.texIndex = static_cast<s16>(it - l.textures.begin());
            break;
        }
        default: break;
    }
}

void applyPane(AnimTarget target, float v, PaneState& s) {
    const u8 t = static_cast<u8>(target);
    if (t >= static_cast<u8>(AnimTarget::VtxColor0) && t < static_cast<u8>(AnimTarget::Alpha)) {
        const u8 k = static_cast<u8>(t - static_cast<u8>(AnimTarget::VtxColor0));
        Rgba& c = s.vtxColor[k / 4];
        u8* ch[4] = {&c.r, &c.g, &c.b, &c.a};
        *ch[k % 4] = channel(v);
        return;
    }
    switch (target) {
        case AnimTarget::TransX: s.trans.x = v; break;
        case AnimTarget::TransY: s.trans.y = v; break;
        case AnimTarget::TransZ: s.trans.z = v; break;
        case AnimTarget::RotX: s.rot.x = v; break;
        case AnimTarget::RotY: s.rot.y = v; break;
        case AnimTarget::RotZ: s.rot.z = v; break;
        case AnimTarget::ScaleX: s.scale.x = v; break;
        case AnimTarget::ScaleY: s.scale.y = v; break;
        case AnimTarget::SizeW: s.size.x = v; break;
        case AnimTarget::SizeH: s.size.y = v; break;
        case AnimTarget::Alpha: s.alpha = channel(v); break;
        case AnimTarget::Visible: s.visible = v != 0.0f; break;
        default: break;
    }
}
}  // namespace

Result<Anim> parseBrlan(std::span<const u8> file) {
    if (file.size() < 0x10 || !is(file, "RLAN") || be16(file, 4) != 0xFEFF) return invalid("BRLAN: kein RLAN");
    if (be32(file, 8) > file.size()) return invalid("BRLAN: Datei abgeschnitten");
    const std::span<const u8> data = file.first(be32(file, 8));
    std::size_t at = be16(file, 0x0C);
    const u32 sections = be16(file, 0x0E);
    Anim a;
    bool havePai = false;
    for (u32 i = 0; i < sections; ++i) {
        if (at > data.size() || data.size() - at < 8) return invalid("BRLAN: Abschnitt hinter dem Ende");
        const u32 size = be32(data, at + 4);
        if (size < 8 || size > data.size() - at) return invalid("BRLAN: Abschnittslänge");
        const std::span<const u8> s = data.subspan(at, size);
        at += size;
        if (is(s, "pai1")) {
            if (havePai || !readPai1(s, a)) return invalid("BRLAN: pai1");
            havePai = true;
        }  // pat1 (Gruppen-Zuordnung) und anderes: für das Bild ohne Bedeutung
    }
    if (!havePai) return invalid("BRLAN: pai1 fehlt");
    return Result<Anim>::ok(std::move(a));
}

float evalTrack(const Track& t, float frame) {
    const auto& k = t.keys;
    if (k.empty()) return 0.0f;
    if (frame <= k.front().frame) return k.front().value;
    if (frame >= k.back().frame) return k.back().value;
    // erster Key mit frame > frame; davor liegt mindestens einer
    const auto hi = std::upper_bound(k.begin(), k.end(), frame, [](float f, const Key& key) { return f < key.frame; });
    const Key& b = *hi;
    const Key& a = *(hi - 1);
    if (t.step) return a.value;
    const float len = b.frame - a.frame;
    if (len <= 0.0f) return b.value;
    const float u = (frame - a.frame) / len, u2 = u * u, u3 = u2 * u;
    return a.value * (2 * u3 - 3 * u2 + 1) + b.value * (-2 * u3 + 3 * u2) + (a.slope * (u3 - 2 * u2 + u) + b.slope * (u3 - u2)) * len;
}

void applyAnim(const Layout& l, const Anim& a, float frame, std::vector<PaneState>& state) {
    if (state.size() != l.panes.size()) return;
    for (const Track& t : a.tracks) {
        if (t.keys.empty()) continue;
        const float v = evalTrack(t, frame);
        if (t.material) {
            for (std::size_t m = 0; m < l.materials.size(); ++m) {
                if (l.materials[m].name != t.pane) continue;
                for (std::size_t p = 0; p < l.panes.size(); ++p)
                    if (l.panes[p].material == static_cast<s32>(m)) applyMaterial(l, a, t.target, v, state[p]);
            }
        } else {
            for (std::size_t p = 0; p < l.panes.size(); ++p)
                if (l.panes[p].name == t.pane) applyPane(t.target, v, state[p]);
        }
    }
}

u64 animWork(const Layout& l, const Anim& a) {
    u64 work = 0;
    for (const Track& t : a.tracks) {
        if (t.keys.empty()) continue;
        if (!t.material) {
            work += l.panes.size();
            continue;
        }
        work += l.materials.size();
        for (const LytMaterial& m : l.materials)
            if (m.name == t.pane) work += l.panes.size();
    }
    return work;
}

void AnimPlayer::reset(const Anim* start, const Anim* loop) {
    start_ = start;
    loop_ = loop;
    current_ = start != nullptr ? start : loop;
    frame_ = 0;
}

void AnimPlayer::step() {
    if (current_ == nullptr || current_->frames == 0) return;
    if (++frame_ < current_->frames) return;
    frame_ = 0;
    if (current_ == start_ && loop_ != nullptr) current_ = loop_;  // Start zu Ende: weiter mit der Schleife
    else if (current_ == start_) frame_ = static_cast<u32>(current_->frames - 1);  // ohne Schleife: letztes Bild halten
}

}  // namespace nl
