#pragma once
// RGBA-Farbe für Theme-Tokens (Spec 3.6), plattformfrei.

#include <cstdint>

namespace nl {

struct Color {
    std::uint8_t r, g, b, a;

    static constexpr Color fromHex(std::uint32_t rgb, std::uint8_t alpha = 0xFF) {
        return Color{static_cast<std::uint8_t>((rgb >> 16) & 0xFF),
                     static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                     static_cast<std::uint8_t>(rgb & 0xFF), alpha};
    }

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

}  // namespace nl
