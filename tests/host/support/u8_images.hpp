#pragma once
// Baut U8-Archive und IMD5-Hüllen für die Banner-Tests (M18). Alles aus eigenen Daten.

#include <cstring>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "support/disc_images.hpp"
#include "support/gc_disc_images.hpp"

namespace nl::test {

// U8: Kopf 0x20 (Magic, Wurzeloffset 0x20, Größe Knoten+Namen, Datenoffset), Knoten zu 12 Byte, Namen, Daten.
// Nutzt FstSpec (Datei: data, Ordner: children).
inline std::vector<u8> buildU8(const std::vector<FstSpec>& root) {
    struct Node {
        u32 w0, w1, w2;
    };
    std::vector<Node> nodes{{0x01000000u, 0, 0}};
    std::string names(1, '\0');  // Wurzel hat den leeren Namen
    std::vector<std::pair<std::size_t, const std::vector<u8>*>> files;
    auto add = [&](auto& self, const std::vector<FstSpec>& list, u32 parent) -> void {
        for (const FstSpec& s : list) {
            const u32 nameOff = static_cast<u32>(names.size());
            names += s.name;
            names += '\0';
            const std::size_t idx = nodes.size();
            if (s.dir) {
                nodes.push_back({0x01000000u | nameOff, parent, 0});
                self(self, s.children, static_cast<u32>(idx));
                nodes[idx].w2 = static_cast<u32>(nodes.size());
            } else {
                nodes.push_back({nameOff, 0, static_cast<u32>(s.data.size())});
                files.emplace_back(idx, &s.data);
            }
        }
    };
    add(add, root, 0);
    nodes[0].w2 = static_cast<u32>(nodes.size());
    const u32 headerSize = static_cast<u32>(nodes.size() * 12 + names.size());
    u32 at = (0x20 + headerSize + 31u) & ~31u;
    const u32 dataStart = at;
    for (auto& [idx, data] : files) {
        nodes[idx].w1 = at;
        at = (at + static_cast<u32>(data->size()) + 31u) & ~31u;
    }
    std::vector<u8> out(at, 0);
    putBe32(out, 0, 0x55AA382Du);
    putBe32(out, 4, 0x20);
    putBe32(out, 8, headerSize);
    putBe32(out, 12, dataStart);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        putBe32(out, 0x20 + i * 12, nodes[i].w0);
        putBe32(out, 0x20 + i * 12 + 4, nodes[i].w1);
        putBe32(out, 0x20 + i * 12 + 8, nodes[i].w2);
    }
    std::memcpy(out.data() + 0x20 + nodes.size() * 12, names.data(), names.size());
    for (auto& [idx, data] : files) std::memcpy(out.data() + nodes[idx].w1, data->data(), data->size());
    return out;
}

// IMD5-Kopf: "IMD5", Größe der Nutzdaten, 8 Byte frei, 16 Byte MD5 (hier Nullen).
inline std::vector<u8> imd5(const std::vector<u8>& payload) {
    std::vector<u8> out(32 + payload.size(), 0);
    std::memcpy(out.data(), "IMD5", 4);
    putBe32(out, 4, static_cast<u32>(payload.size()));
    std::memcpy(out.data() + 32, payload.data(), payload.size());
    return out;
}

}  // namespace nl::test
