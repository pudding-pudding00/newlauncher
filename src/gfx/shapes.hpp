#pragma once
// Formen aus dem UI-Atlas: ein weißer Block für Flächen, ein geglätteter Kreis für Rundungen.
// So bleiben Flächen, Rundungen und Text in einer Textur und damit in einem Draw-Call.

#include "gfx/draw_list.hpp"

namespace nl::gfx {

struct AtlasShapes {
    UvRect white;   // Innenbereich eines vollständig deckenden Blocks
    UvRect circle;  // vollständiger Kreis mit geglättetem Rand
};

void fillRect(DrawList& dl, const AtlasShapes& s, Rect r, Color c);
// 7 Quads: Mittelstreifen, linker und rechter Streifen, 4 Kreisviertel. Radius wird auf
// min(w, h) / 2 geklemmt; Radius 0 ergibt ein Quad, leere Rechtecke keins.
void roundRect(DrawList& dl, const AtlasShapes& s, Rect r, float radius, Color c);
void circle(DrawList& dl, const AtlasShapes& s, float cx, float cy, float r, Color c);

}  // namespace nl::gfx
