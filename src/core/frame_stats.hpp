#pragma once
// Frame-Statistik für das Debug-Overlay (Spec 4.1/7).

#include <cstddef>

#include "core/types.hpp"

namespace nl {

struct FrameStats {
    u32 cpuMicros = 0, drawCalls = 0, quads = 0, droppedQuads = 0, textureBytes = 0;
};

// "CPU 4.2 ms | DC 1 | Q 310 | Tex 512 KB" (+ " | DROP n"); nullterminiert, ohne Allokation.
// Liefert die geschriebene Länge; cap 0 schreibt nichts.
int formatStats(const FrameStats& s, char* out, std::size_t cap);

}  // namespace nl
