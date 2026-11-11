// Brücke vom C-Booter zu nl::lz77Decompress (M11 Task 1). Der ChannelHandler gibt das Ergebnis mit
// free() frei, deshalb wird in malloc-Speicher kopiert.
#include "nl/lz77_bridge.h"

#include <cstdlib>
#include <cstring>

#include "core/lz77.hpp"

extern "C" int isLZ77compressed(const u8* buffer) {
    if (buffer == nullptr) return 0;
    if (buffer[0] == 'L') {
        if (buffer[1] != 'Z' || buffer[2] != '7' || buffer[3] != '7') return 0;
        return (buffer[4] == 0x10 || buffer[4] == 0x11) ? 1 : 0;
    }
    return (buffer[0] == 0x10 || buffer[0] == 0x11) ? 1 : 0;
}

extern "C" int decompressLZ77content(const u8* buffer, u32 length, u8** output, u32* outputLen) {
    if (buffer == nullptr || output == nullptr || outputLen == nullptr) return -1;
    auto r = nl::lz77Decompress({buffer, length});
    if (!r.isOk()) return -1;
    const auto& data = r.value();
    auto* mem = static_cast<u8*>(std::malloc(data.size()));
    if (mem == nullptr) return -1;
    std::memcpy(mem, data.data(), data.size());
    *output = mem;
    *outputLen = static_cast<u32>(data.size());
    return 0;
}
