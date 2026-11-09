#pragma once
// Textmessung und -layout ohne Allokation; alle Funktionen arbeiten auf UTF-8-string_views.

#include "gfx/font.hpp"

#include <span>

namespace nl::gfx {

float measureText(const FontFace& f, std::string_view s);
void drawText(DrawList& dl, const FontFace& f, float x, float baselineY, std::string_view s, Color c);
// Passt der Text nicht in maxWidth, wird er gekürzt und mit „…" beendet.
void drawTextEllipsis(DrawList& dl, const FontFace& f, float x, float baselineY, float maxWidth,
                      std::string_view s, Color c);

struct Line {
    u16 start, length;  // Byte-Bereich im Quelltext
};
// Bricht an Leerzeichen, überlange Wörter hart. Rückgabe = benutzte Zeilen (≤ out.size()).
// Passt der Text nicht in out.size() Zeilen, reicht die letzte Zeile bis zum Textende,
// damit der Aufrufer sie mit drawTextEllipsis zeichnet.
int wrapText(const FontFace& f, std::string_view s, float maxWidth, std::span<Line> out);

}  // namespace nl::gfx
