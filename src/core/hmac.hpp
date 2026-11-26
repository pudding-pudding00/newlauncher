#pragma once
// HMAC-SHA256 nach RFC 2104 (M27, Token-Speicher) und ein Vergleich ohne frühen Abbruch.

#include <array>
#include <span>

#include "core/types.hpp"

namespace nl {

std::array<u8, 32> hmacSha256(std::span<const u8> key, std::span<const u8> msg);
// Laufzeit hängt nur von der Länge ab; ungleiche Längen → false.
bool constantTimeEqual(std::span<const u8> a, std::span<const u8> b);

}  // namespace nl
