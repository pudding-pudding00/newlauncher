#pragma once
// Cover auf 200×280 einpassen (Spec 6.4): Seitenverhältnis bleibt, zentriert, freie Ränder transparent.

#include <span>
#include <vector>

#include "core/types.hpp"

namespace nl {

// rgba: w*h Pixel zu je 4 Byte. Ergebnis kCoverW*kCoverH*4 Byte, leer bei w/h == 0 oder zu kleinem Puffer.
// Verkleinern mittelt alle Quellpixel einer Zielzelle (alpha-gewichtet), Vergrößern nimmt den nächsten Pixel.
std::vector<u8> fitCover(std::span<const u8> rgba, u32 w, u32 h);

}  // namespace nl
