#pragma once
// CMPR-Encoder (Spec 5.5, 8.1): RGBA8 → GX-CMPR (DXT1 in 8×8-Kacheln aus vier 4×4-Unterblöcken).

#include <span>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kGxFormatCmpr = 14;

// rgba: 4 Byte je Pixel, zeilenweise. Breite und Höhe Vielfache von 8, sonst leeres Ergebnis.
// Ergebnis w*h/2 Byte, Big-Endian. Pixel mit Alpha < 128 werden transparent (3-Farben-Modus, Index 3).
std::vector<u8> encodeCmpr(std::span<const u8> rgba, u16 width, u16 height);

}  // namespace nl
