#pragma once
// Inflate für rohe Deflate-Ströme (RFC 1951) aus ZIP-Einträgen (Spec 6.9). Eigener kleiner Dekoder nach dem
// Vorbild von zlibs „puff“: eine Codebasis für Host und Wii, keine zusätzliche Bibliothek.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

// outSize ist die erwartete Länge (aus dem ZIP-Verzeichnis). Mehr oder weniger Ausgabe, ungültige Codes,
// Rückverweise vor den Anfang oder eine Eingabe, die vor dem letzten Block endet → ErrorCode::InvalidData.
Result<std::vector<u8>> inflateRaw(std::span<const u8> in, std::size_t outSize);

}  // namespace nl
