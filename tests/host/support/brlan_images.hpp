#pragma once
// Baut BRLAN-Dateien für die Host-Tests (M18) aus eigenen Daten.

#include <string>
#include <vector>

#include "support/brlyt_images.hpp"

namespace nl::test {

struct InfoSpec {
    u8 index = 0;   // SRT- bzw. Textur-Map-Index (RLTS/RLTP)
    u8 target = 0;  // Ziel innerhalb des Spurtyps
    bool step = false;
    std::vector<std::array<float, 3>> keys;  // frame, value, slope (step: value als u16)
};

struct TagSpec {
    std::string magic;  // RLPA, RLVC, RLTS, RLTP, RLMC, RLVI
    std::vector<InfoSpec> infos;
};

struct EntrySpec {
    std::string name;
    bool material = false;
    std::vector<TagSpec> tags;
};

inline std::vector<u8> buildBrlan(u16 frames, bool loop, const std::vector<std::string>& patterns,
                                  const std::vector<EntrySpec>& entries) {
    using namespace detail;
    std::vector<u8> b;  // Inhalt von pai1 ab Abschnittsanfang +8
    be16(b, frames);
    b.push_back(loop ? 1 : 0);
    b.push_back(0);
    be16(b, static_cast<u16>(patterns.size()));
    be16(b, static_cast<u16>(entries.size()));
    be32(b, 0);  // Versatz der Eintragstabelle, unten gesetzt
    const std::size_t timgTable = b.size();
    b.resize(b.size() + patterns.size() * 4, 0);
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        putBe32(b, timgTable + i * 4, static_cast<u32>(b.size() - timgTable));  // relativ zur Tabelle
        for (char c : patterns[i]) b.push_back(static_cast<u8>(c));
        b.push_back(0);
    }
    while (b.size() % 4) b.push_back(0);
    const std::size_t entryTable = b.size();
    putBe32(b, 8, static_cast<u32>(8 + entryTable));
    b.resize(b.size() + entries.size() * 4, 0);
    for (std::size_t e = 0; e < entries.size(); ++e) {
        const EntrySpec& en = entries[e];
        const std::size_t base = b.size();
        putBe32(b, entryTable + e * 4, static_cast<u32>(8 + base));  // relativ zum Abschnittsanfang
        name(b, en.name, 20);
        b.push_back(static_cast<u8>(en.tags.size()));
        b.push_back(en.material ? 1 : 0);
        be16(b, 0);
        const std::size_t tagTable = b.size();
        b.resize(b.size() + en.tags.size() * 4, 0);
        for (std::size_t t = 0; t < en.tags.size(); ++t) {
            const TagSpec& tag = en.tags[t];
            const std::size_t tagAt = b.size();
            putBe32(b, tagTable + t * 4, static_cast<u32>(tagAt - base));  // relativ zum Eintrag
            for (char c : tag.magic) b.push_back(static_cast<u8>(c));
            b.push_back(static_cast<u8>(tag.infos.size()));
            b.push_back(0);
            be16(b, 0);
            const std::size_t infoTable = b.size();
            b.resize(b.size() + tag.infos.size() * 4, 0);
            for (std::size_t i = 0; i < tag.infos.size(); ++i) {
                const InfoSpec& in = tag.infos[i];
                const std::size_t infoAt = b.size();
                putBe32(b, infoTable + i * 4, static_cast<u32>(infoAt - tagAt));  // relativ zum Tag
                b.push_back(in.index);
                b.push_back(in.target);
                b.push_back(in.step ? 1 : 2);
                b.push_back(0);
                be16(b, static_cast<u16>(in.keys.size()));
                be16(b, 0);
                be32(b, 0x0C);
                for (const auto& k : in.keys) {
                    f32(b, k[0]);
                    if (in.step) {
                        be16(b, static_cast<u16>(k[1]));
                        be16(b, 0);
                    } else {
                        f32(b, k[1]);
                        f32(b, k[2]);
                    }
                }
            }
        }
    }
    Sections s;
    s.add("pai1", b);
    std::vector<u8> out{'R', 'L', 'A', 'N', 0xFE, 0xFF, 0x00, 0x08};
    be32(out, static_cast<u32>(0x10 + s.bytes.size()));
    be16(out, 0x10);
    be16(out, s.count);
    out.insert(out.end(), s.bytes.begin(), s.bytes.end());
    return out;
}

}  // namespace nl::test
