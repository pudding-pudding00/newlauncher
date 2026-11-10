#pragma once
// Speicherlayout des Chainloaders (Spec 9, M3). Der Bereich [kDolBufferAddr, kReservedEnd) in MEM2
// gehört dem Launcher: main() senkt ArenaHi2 als Erstes darunter, damit kein Allokator ihn belegt.
// Dort liegen hintereinander der Lesepuffer für die Ziel-DOL, der App-Booter und der argv-Block.

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kMem1Begin = 0x80003F00;  // erste Adresse hinter den Systemvariablen
inline constexpr u32 kMem1End = 0x81800000;
inline constexpr u32 kMem2Begin = 0x90000000;

inline constexpr u32 kDolBufferAddr = 0x92800000;
inline constexpr u32 kBooterAddr = 0x93300000;
inline constexpr u32 kArgsAddr = 0x93300800;
inline constexpr u32 kReservedEnd = 0x933E0000;
inline constexpr u32 kMaxDolBytes = kBooterAddr - kDolBufferAddr;  // 11 MiB

}  // namespace nl
