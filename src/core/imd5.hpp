#pragma once
// IMD5-Hülle um banner.bin/icon.bin/sound.bin (M18): 32-Byte-Kopf ("IMD5", Größe, frei, MD5),
// danach die Nutzdaten, oft LZ77-gepackt (0x10/0x11). Die MD5 wird nicht geprüft.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

// Nutzdaten ab Offset 32, entpackt falls LZ77. Fehler: kein IMD5, Größe hinter dem Ende, Ergebnis > maxOut.
Result<std::vector<u8>> unwrapImd5(std::span<const u8> data, u32 maxOut);

}  // namespace nl
