#include "core/crc32.hpp"

#include <array>

namespace nl {

namespace {
constexpr std::array<u32, 256> makeTable() {
    std::array<u32, 256> t{};
    for (u32 n = 0; n < 256; ++n) {
        u32 c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}
constexpr std::array<u32, 256> kTable = makeTable();
}  // namespace

u32 crc32(std::span<const u8> data, u32 crc) {
    crc = ~crc;
    for (u8 b : data) crc = kTable[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

}  // namespace nl
