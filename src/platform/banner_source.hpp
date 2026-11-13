#pragma once
// Banner-Quellen auf der Wii (M18): Content 0 eines Kanals aus dem NAND über ES und ISFS.

#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl::platform {

inline constexpr u32 kChannelContent0Max = 4u * 1024 * 1024;

// Ganzer Content 0 (IMET + U8). Größe aus ISFS_GetFileStats, vor dem Allozieren gegen
// kChannelContent0Max geprüft. Fehler: NotFound (kein TMD/keine Datei), IoError, InvalidData (zu groß).
Result<std::vector<u8>> readChannelContent0(u64 titleId);

}  // namespace nl::platform
