#pragma once
// Baut DOL-Dateien für Tests: Big-Endian-Header (0x100 Byte) und Sektionsdaten mit Füllbytes.
// Liegt der Offset einer Sektion bei 0, wird sie hinter die vorige gelegt.

#include <vector>

#include "core/dol.hpp"
#include "support/disc_images.hpp"

namespace nl::test {

inline std::vector<u8> dolImage(std::vector<DolSection> text, std::vector<DolSection> data, u32 entry,
                                u32 bssAddr = 0, u32 bssSize = 0) {
    std::vector<u8> b(kDolHeaderBytes, 0);
    u32 next = kDolHeaderBytes;
    auto place = [&](DolSection& s) {
        if (s.size == 0) return;
        if (s.offset == 0) s.offset = next;
        if (b.size() < s.offset + s.size) b.resize(s.offset + s.size, 0xA5);
        next = s.offset + s.size;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        place(text[i]);
        putBe32(b, 0x00 + i * 4, text[i].offset);
        putBe32(b, 0x48 + i * 4, text[i].address);
        putBe32(b, 0x90 + i * 4, text[i].size);
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        place(data[i]);
        putBe32(b, 0x1C + i * 4, data[i].offset);
        putBe32(b, 0x64 + i * 4, data[i].address);
        putBe32(b, 0xAC + i * 4, data[i].size);
    }
    putBe32(b, 0xD8, bssAddr);
    putBe32(b, 0xDC, bssSize);
    putBe32(b, 0xE0, entry);
    return b;
}

}  // namespace nl::test
