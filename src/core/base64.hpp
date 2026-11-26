#pragma once
// Base64 nach RFC 4648 (M27): Standard mit Auffüllung, URL-Variante (-_) ohne Auffüllung.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

std::string base64Encode(std::span<const u8> data, bool url = false);
// Streng: falsches Alphabet, falsche Auffüllung oder Länge → nullopt. Die URL-Variante akzeptiert keine '='.
std::optional<std::vector<u8>> base64Decode(std::string_view text, bool url = false);

}  // namespace nl
