#pragma once
// Deflate-Kompressor (RFC 1951) für Spielstand-Pakete (M26, Spec Ziel 2). Eigener kleiner Kodierer: LZ77 mit
// Hash-Ketten und festen Huffman-Codes; lohnt sich das nicht, gespeicherte Blöcke. Gegenstück: inflateRaw.

#include <span>
#include <vector>

#include "core/types.hpp"

namespace nl {

// Roher Deflate-Strom ohne zlib-Kopf. Ausgabe nie größer als Eingabe + 5 Bytes je 65535 Bytes (+5 bei leer).
std::vector<u8> deflateRaw(std::span<const u8> in);

}  // namespace nl
