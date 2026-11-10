#pragma once
// .tex-Datei (Spec 5.5): "NLTX" | u16 breite | u16 höhe | u32 gx_format | u32 datenlänge, Kopf auf 32 Byte
// aufgefüllt, Daten ab Offset 32, Big-Endian.

#include <span>
#include <vector>

#include "core/cmpr.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u16 kCoverW = 200, kCoverH = 280;
inline constexpr u32 kCoverTexBytes = u32(kCoverW) * kCoverH / 2;
inline constexpr u32 kTexHeaderBytes = 32;

std::vector<u8> writeTex(u16 w, u16 h, std::span<const u8> cmpr);

struct TexView {
    u16 width, height;
    std::span<const u8> data;
};

// Prüft Magic, Format CMPR, datenlänge == w*h/2 und Dateilänge ≥ 32 + datenlänge.
Result<TexView> parseTex(std::span<const u8> file);

}  // namespace nl
