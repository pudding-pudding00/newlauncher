#pragma once
// CRC-32 wie zlib/ZIP (Polynom 0xEDB88320), für die Prüfung entpackter Emulator-Dateien (Spec 6.9).

#include <span>

#include "core/types.hpp"

namespace nl {

// crc ist der Wert der bisherigen Daten (0 am Anfang); so lässt sich stückweise rechnen.
u32 crc32(std::span<const u8> data, u32 crc = 0);

}  // namespace nl
