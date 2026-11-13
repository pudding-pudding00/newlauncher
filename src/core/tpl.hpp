#pragma once
// TPL-Texturdateien aus Wii-Bannern (M18). Die Bilddaten bleiben im GX-Kachelformat und gehen direkt an GX;
// decodeToRgba gibt es für Host-Tests und als Rückfallebene. Eingaben gelten als nicht vertrauenswürdig.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

enum class TexFmt : u8 { I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA8 = 6, C4 = 8, C8 = 9, C14X2 = 10, CMPR = 14 };

inline constexpr u16 kTplMaxSide = 1024;
inline constexpr u32 kTplMaxImages = 256;

struct TplImage {
    TexFmt fmt = TexFmt::RGBA8;
    u16 w = 0, h = 0;
    std::span<const u8> data;           // texBytes(fmt, w, h) Byte im GX-Kachelformat
    TexFmt palFmt = TexFmt::RGB5A3;     // nur C4/C8/C14X2: IA8, RGB565 oder RGB5A3
    std::span<const u8> palette;        // palCount × 2 Byte, Big-Endian
    u16 palCount = 0;
};

// Magic 00 20 AF 30. Fehler (InvalidData): Seite 0 oder > 1024, unbekanntes Format, Daten oder Palette
// hinter dem Ende, Palettenformat ohne Palette oder mit unbekanntem Palettenformat.
Result<std::vector<TplImage>> parseTpl(std::span<const u8> file);

// Größe im GX-Kachelformat: Breite und Höhe auf ganze Kacheln gerundet. 0 bei unbekanntem Format.
u32 texBytes(TexFmt f, u16 w, u16 h);

// RGBA8, zeilenweise, w*h*4 Byte. Palettenindizes außerhalb der Palette werden transparent schwarz.
std::vector<u8> decodeToRgba(const TplImage& img);

}  // namespace nl
