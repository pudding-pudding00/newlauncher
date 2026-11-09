#pragma once
// UTF-8-Dekodierung, robust gegen ungültige Eingaben (gemeinsam für Text-Rendering und Suche).

#include <cstddef>
#include <string_view>

#include "core/types.hpp"

namespace nl {

// Liest einen Codepoint ab `pos` und rückt `pos` um mindestens 1 vor.
// Ungültige, überlange, abgeschnittene Folgen und Surrogate ergeben '?'.
u32 decodeUtf8(std::string_view s, std::size_t& pos);

}  // namespace nl
