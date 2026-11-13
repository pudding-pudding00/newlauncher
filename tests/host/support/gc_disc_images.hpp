#pragma once
// Baut GC-Abbilder mit FST und opening.bnr für die Banner-Tests (M18). Alles aus eigenen Daten.

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/types.hpp"
#include "support/disc_images.hpp"

namespace nl::test {

// FST-Eintrag zum Bauen: Datei (data) oder Ordner (children).
struct FstSpec {
    std::string name;
    std::vector<u8> data;
    bool dir = false;
    std::vector<FstSpec> children;
};

namespace detail {
struct FstBuild {
    std::vector<std::array<u32, 3>> entries;  // Wort 0: Flag|Namensoffset, 1: Offset/Eltern, 2: Größe/next
    std::string names;
    std::vector<std::pair<std::size_t, const std::vector<u8>*>> payloads;  // Eintrag → Daten
};
inline void addEntries(FstBuild& b, const std::vector<FstSpec>& list, u32 parent) {
    for (const FstSpec& s : list) {
        const u32 nameOff = static_cast<u32>(b.names.size());
        b.names += s.name;
        b.names += '\0';
        const std::size_t idx = b.entries.size();
        if (s.dir) {
            b.entries.push_back({0x01000000u | nameOff, parent, 0});
            addEntries(b, s.children, static_cast<u32>(idx));
            b.entries[idx][2] = static_cast<u32>(b.entries.size());
        } else {
            b.entries.push_back({nameOff, 0, static_cast<u32>(s.data.size())});
            b.payloads.emplace_back(idx, &s.data);
        }
    }
}
}  // namespace detail

// FST mit Wurzel; Dateioffsets ab dataStart fortlaufend (32-Byte-ausgerichtet). blob erhält die Dateidaten
// an ihren Offsets (blob[0] = Disc-Offset 0).
inline std::vector<u8> buildFst(const std::vector<FstSpec>& root, u32 dataStart, std::vector<u8>* blob = nullptr) {
    detail::FstBuild b;
    b.entries.push_back({0x01000000u, 0, 0});
    detail::addEntries(b, root, 0);
    b.entries[0][2] = static_cast<u32>(b.entries.size());
    u32 at = dataStart;
    for (auto& [idx, data] : b.payloads) {
        b.entries[idx][1] = at;
        if (blob != nullptr) {
            if (blob->size() < at + data->size()) blob->resize(at + data->size(), 0);
            std::memcpy(blob->data() + at, data->data(), data->size());
        }
        at = (at + static_cast<u32>(data->size()) + 31u) & ~31u;
    }
    std::vector<u8> out(b.entries.size() * 12 + b.names.size(), 0);
    for (std::size_t i = 0; i < b.entries.size(); ++i)
        for (int w = 0; w < 3; ++w) putBe32(out, i * 12 + static_cast<std::size_t>(w) * 4, b.entries[i][static_cast<std::size_t>(w)]);
    std::memcpy(out.data() + b.entries.size() * 12, b.names.data(), b.names.size());
    return out;
}

// GC-ISO: Kopf, FST bei 0x2000 (Offset/Größe bei 0x424/0x428), Dateien dahinter.
inline std::vector<u8> gcIsoWithFiles(std::string_view id6, std::string_view title, const std::vector<FstSpec>& root) {
    constexpr u32 kFst = 0x2000;
    std::vector<u8> img = gcIso(id6, title, kFst);
    std::vector<u8> probe = buildFst(root, 0);
    const u32 dataStart = (kFst + static_cast<u32>(probe.size()) + 31u) & ~31u;
    std::vector<u8> fst = buildFst(root, dataStart, &img);
    if (img.size() < kFst + fst.size()) img.resize(kFst + fst.size(), 0);
    std::memcpy(img.data() + kFst, fst.data(), fst.size());
    putBe32(img, 0x424, kFst);
    putBe32(img, 0x428, static_cast<u32>(fst.size()));
    return img;
}

// CISO aus einem GC-Abbild: Blockgröße block, nur nicht leere Blöcke werden gespeichert.
inline std::vector<u8> cisoFromIso(const std::vector<u8>& iso, u32 block = 0x8000) {
    std::vector<u8> out(0x8000, 0);
    std::memcpy(out.data(), "CISO", 4);
    out[4] = static_cast<u8>(block);
    out[5] = static_cast<u8>(block >> 8);
    out[6] = static_cast<u8>(block >> 16);
    out[7] = static_cast<u8>(block >> 24);
    const std::size_t blocks = (iso.size() + block - 1) / block;
    for (std::size_t b = 0; b < blocks; ++b) {
        const std::size_t from = b * block, n = std::min<std::size_t>(block, iso.size() - from);
        bool empty = true;
        for (std::size_t i = 0; i < n && empty; ++i) empty = iso[from + i] == 0;
        if (empty) continue;
        out[8 + b] = 1;
        const std::size_t at = out.size();
        out.resize(at + block, 0);
        std::memcpy(out.data() + at, iso.data() + from, n);
    }
    return out;
}

// Textblock eines BNR: Kurztitel, Kurzhersteller, Titel, Hersteller, Beschreibung.
struct BnrText {
    std::string shortTitle, shortMaker, title, maker, description;
};

// BNR1 (ein Block) oder BNR2 (sechs Blöcke). Bild: Pixel 0 = px0, Pixel 1 = px1, Rest 0.
inline std::vector<u8> gcBnr(bool bnr2, const std::vector<BnrText>& blocks, u16 px0 = 0xFC00, u16 px1 = 0x7F00) {
    const std::size_t count = bnr2 ? 6 : 1;
    std::vector<u8> b(0x1820 + count * 0x140, 0);
    std::memcpy(b.data(), bnr2 ? "BNR2" : "BNR1", 4);
    b[0x20] = static_cast<u8>(px0 >> 8);
    b[0x21] = static_cast<u8>(px0);
    b[0x22] = static_cast<u8>(px1 >> 8);
    b[0x23] = static_cast<u8>(px1);
    auto put = [&](std::size_t at, const std::string& s, std::size_t cap) {
        std::memcpy(b.data() + at, s.data(), std::min(s.size(), cap));
    };
    for (std::size_t i = 0; i < count && i < blocks.size(); ++i) {
        const std::size_t base = 0x1820 + i * 0x140;
        put(base, blocks[i].shortTitle, 0x20);
        put(base + 0x20, blocks[i].shortMaker, 0x20);
        put(base + 0x40, blocks[i].title, 0x40);
        put(base + 0x80, blocks[i].maker, 0x40);
        put(base + 0xC0, blocks[i].description, 0x80);
    }
    return b;
}

}  // namespace nl::test
