#pragma once
// CMPR-Encoder (Spec 5.5, 8.1): RGBA8 → GX-CMPR (DXT1 in 8×8-Kacheln aus vier 4×4-Unterblöcken).

#include <optional>
#include <span>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kGxFormatCmpr = 14;

// rgba: 4 Byte je Pixel, zeilenweise. Breite und Höhe Vielfache von 8, sonst leeres Ergebnis.
// Ergebnis w*h/2 Byte, Big-Endian. Pixel mit Alpha < 128 werden transparent (3-Farben-Modus, Index 3).
std::vector<u8> encodeCmpr(std::span<const u8> rgba, u16 width, u16 height);

// M23: Pixelbereich [x0,x1)×[y0,y1).
struct PixelBox {
    u16 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// Umschließender Bereich aller 4×4-Blöcke, die nicht ganz transparent sind (3-Farben-Modus, alle Indizes 3).
// So findet die UI den Inhalt eines eingepassten Covers ohne eigenes Feld in der .tex-Datei.
// nullopt, wenn alles transparent ist oder die Daten zu kurz sind.
std::optional<PixelBox> cmprOpaqueBox(std::span<const u8> cmpr, u16 width, u16 height);

}  // namespace nl
