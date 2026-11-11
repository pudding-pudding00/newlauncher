#pragma once
// LZ77-Dekoder für Nintendo-Inhalte (Typ 0x10 und 0x11, optional mit "LZ77"-Präfix), Spec v2 3.2.
// Eigene Implementierung nach der Formatbeschreibung im M11-Plan; ersetzt den GPLv2-only-Code im Booter.
// Die Eingabe stammt aus dem NAND und gilt als nicht vertrauenswürdig: jeder Zugriff ist grenzgeprüft.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kLz77MaxOut = 16u * 1024 * 1024;

// Typbyte 0x10/0x11 am Anfang, oder Präfix "LZ77" gefolgt von 0x10/0x11. Eingaben unter 4 Bytes: false.
bool isLz77(std::span<const u8> in);

// Entpackt in genau die im Kopf angekündigte Größe. Fehler (InvalidData): Kopf zu kurz, fremdes Typbyte,
// Größe 0 oder über maxOut (dann ohne Allokation), Rückverweis vor den Anfang, Eingabe endet zu früh.
Result<std::vector<u8>> lz77Decompress(std::span<const u8> in, u32 maxOut = kLz77MaxOut);

}  // namespace nl
