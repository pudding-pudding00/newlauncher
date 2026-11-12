#pragma once
// Baut minimale Disc-Header für Tests (nur Header, keine Spieldaten).

#include <cstring>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl::test {

inline void putBe32(std::vector<u8>& b, std::size_t at, u32 v) {
    b[at] = static_cast<u8>(v >> 24);
    b[at + 1] = static_cast<u8>(v >> 16);
    b[at + 2] = static_cast<u8>(v >> 8);
    b[at + 3] = static_cast<u8>(v);
}

inline void putHeader(std::vector<u8>& b, std::size_t base, std::string_view id6, std::string_view title, bool wii) {
    std::memcpy(b.data() + base, id6.data(), id6.size() < 6 ? id6.size() : 6);
    if (wii) putBe32(b, base + 0x18, 0x5D1C9EA3);
    else putBe32(b, base + 0x1C, 0xC2339F3D);
    std::memcpy(b.data() + base + 0x20, title.data(), title.size() < 64 ? title.size() : 64);
}

inline std::vector<u8> wiiIso(std::string_view id6, std::string_view title, std::size_t size = 1024) {
    std::vector<u8> b(size, 0);
    putHeader(b, 0, id6, title, true);
    return b;
}

inline std::vector<u8> gcIso(std::string_view id6, std::string_view title, std::size_t size = 1024) {
    std::vector<u8> b(size, 0);
    putHeader(b, 0, id6, title, false);
    return b;
}

inline std::vector<u8> wbfsFile(std::string_view id6, std::string_view title, u8 hdSecShift = 9, std::size_t size = 1024) {
    std::vector<u8> b(size, 0);
    std::memcpy(b.data(), "WBFS", 4);
    putBe32(b, 4, 1);
    b[8] = hdSecShift;
    b[9] = 21;
    const std::size_t base = std::size_t{1} << hdSecShift;
    if (base + 0x100 <= size) putHeader(b, base, id6, title, true);
    return b;
}

// CISO (Nintendont/USB Loader GX): „CISO“, Blockgröße 2 MiB (LE), Blockkarte; der GC-Disc-Kopf steht bei 0x8000.
inline std::vector<u8> cisoFile(std::string_view id6, std::string_view title) {
    std::vector<u8> b(0x8000 + 1024, 0);
    std::memcpy(b.data(), "CISO", 4);
    b[6] = 0x20;  // 0x00200000 little endian
    b[8] = 1;     // Block 0 belegt
    putHeader(b, 0x8000, id6, title, false);
    return b;
}

}  // namespace nl::test
