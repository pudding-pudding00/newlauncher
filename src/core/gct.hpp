#pragma once
// GCT für Codehandler und Nintendont: 00D0C0DE 00D0C0DE, die Codewörter, F0000000 00000000 (big-endian).

#include <span>
#include <vector>

#include "core/gecko_txt.hpp"
#include "core/types.hpp"

namespace nl {

std::vector<u8> buildGct(std::span<const GeckoCode* const> codes);

}  // namespace nl
