#pragma once
// Rohzugriff auf ein Blockgerät (SD oder USB) in Sektoren, für die Fragmentliste der Wii-Spiele (M9).

#include "core/types.hpp"

#include <span>

namespace nl {

struct BlockDevice {
    virtual ~BlockDevice() = default;
    // Liest `count` Sektoren ab `lba` nach `out` (mindestens count × sectorSize() Byte); false bei Fehler.
    virtual bool read(u32 lba, u32 count, std::span<u8> out) = 0;
    virtual u32 sectorSize() const = 0;
};

}  // namespace nl
