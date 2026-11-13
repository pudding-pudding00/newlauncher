#pragma once
// Hintergrundbild eines Themes (Spec v2 10.2): RGBA8 → GX-Textur RGB5A3 in 4×4-Kacheln. Plattformfrei.

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kMaxBackgroundWidth = 640, kMaxBackgroundHeight = 480;
inline constexpr std::size_t kMaxBackgroundTextureBytes = 1258291;  // 1,2 MiB
inline constexpr std::size_t kMaxBackgroundFileBytes = 2u * 1024 * 1024;

struct Rgb5a3Image {
    u16 width = 0, height = 0;  // auf Vielfache von 4 aufgefüllt
    std::vector<u8> texels;     // big-endian u16 je Texel
};

// Deckend (a = 255) → 1rrrrrgggggbbbbb, sonst 0aaarrrrggggbbbb. Auffüllung ist durchsichtig.
// rgba zu klein für width×height → leeres Bild.
Rgb5a3Image encodeRgb5a3(std::span<const u8> rgba, u16 width, u16 height);
// Breite und Höhe aus dem IHDR-Chunk, ohne zu dekodieren; keine PNG-Signatur → nullopt.
std::optional<std::pair<u32, u32>> pngSize(std::span<const u8> file);
bool backgroundFits(u32 width, u32 height);

}  // namespace nl
