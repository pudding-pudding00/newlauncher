#pragma once
// Tonsynthese für die UI-Klänge: plattformfrei, damit Länge, Hüllkurve und Pegel host-getestet sind.

#include <vector>

#include "core/types.hpp"
#include "core/ui_sound.hpp"

namespace nl {

// Mono, 16 Bit. 5 ms linearer Anstieg, danach linearer Abfall auf 0 am letzten Tonsample.
// Länge sampleRate * ms / 1000, mit Nullen aufgefüllt auf ein Vielfaches von 16 Samples (32 Byte, DMA).
std::vector<s16> synthTone(const ToneSpec& spec, u32 sampleRate);

}  // namespace nl
