#pragma once
// UTF-8-Dekodierung, robust gegen ungültige Eingaben (gemeinsam für Text-Rendering und Suche).

#include <cstddef>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

// Liest einen Codepoint ab `pos` und rückt `pos` um mindestens 1 vor.
// Ungültige, überlange, abgeschnittene Folgen und Surrogate ergeben '?'.
u32 decodeUtf8(std::string_view s, std::size_t& pos);

}  // namespace nl

namespace nl {

// Hängt einen Codepoint als UTF-8 an. Surrogate und Werte über 0x10FFFF werden zu U+FFFD.
void appendUtf8(std::string& out, u32 codepoint);

}  // namespace nl
