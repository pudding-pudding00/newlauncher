#pragma once
// GC-Banner opening.bnr (M18): „BNR1“ (ein Textblock) oder „BNR2“ (sechs Blöcke EN, DE, FR, ES, IT, NL).
// Bild 96×32 RGB5A3 (GX-Kacheln 4×4) bei 0x20, 0x1800 Byte; Textblöcke ab 0x1820 zu je 0x140 Byte:
// Kurztitel 0x20, Kurzhersteller 0x20, Titel 0x40, Hersteller 0x40, Beschreibung 0x80.
// Formatbeschreibung: WiiBrew/YAGCD „opening.bnr“; nur die Struktur übernommen, kein Code.

#include <array>
#include <span>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u16 kGcBannerWidth = 96;
inline constexpr u16 kGcBannerHeight = 32;

struct GcBanner {
    std::array<u8, 96 * 32 * 2> rgb5a3;  // GX-Kachelformat, direkt als Textur nutzbar
    std::string shortTitle, title, maker, description;  // UTF-8
};

// langIndex wie BNR2 (0 = EN, 1 = DE, …); bei BNR1 ohne Wirkung, außerhalb → 0. Texte sind Latin-1, bei
// shiftJis (Region J) Shift-JIS: ASCII bleibt, Doppelbytezeichen werden zu „?“. Fehler: InvalidData.
Result<GcBanner> parseGcBanner(std::span<const u8> bnr, u8 langIndex, bool shiftJis = false);

// RGB5A3 in Kacheln → RGBA8 zeilenweise. w, h Vielfache von 4; Eingabe zu kurz → leer.
std::vector<u8> rgb5a3ToRgba(std::span<const u8> tiled, u16 w, u16 h);

}  // namespace nl
