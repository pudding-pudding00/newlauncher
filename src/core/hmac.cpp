#include "core/hmac.hpp"

#include "core/sha256.hpp"

namespace nl {

std::array<u8, 32> hmacSha256(std::span<const u8> key, std::span<const u8> msg) {
    std::array<u8, 64> k{};
    if (key.size() > k.size()) {
        const auto h = sha256(key);
        for (std::size_t i = 0; i < h.size(); ++i) k[i] = h[i];
    } else {
        for (std::size_t i = 0; i < key.size(); ++i) k[i] = key[i];
    }
    std::array<u8, 64> pad{};
    for (std::size_t i = 0; i < 64; ++i) pad[i] = static_cast<u8>(k[i] ^ 0x36);
    Sha256 inner;
    inner.update(pad);
    inner.update(msg);
    const auto ih = inner.finish();
    for (std::size_t i = 0; i < 64; ++i) pad[i] = static_cast<u8>(k[i] ^ 0x5c);
    Sha256 outer;
    outer.update(pad);
    outer.update(ih);
    return outer.finish();
}

bool constantTimeEqual(std::span<const u8> a, std::span<const u8> b) {
    if (a.size() != b.size()) return false;
    u8 diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<u8>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

}  // namespace nl
