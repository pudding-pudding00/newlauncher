#pragma once
// Banner-Quellen auf der Wii (M18): Content 0 eines Kanals aus dem NAND über ES und ISFS;
// opening.bnr eines Wii-Spiels (.iso/.wbfs) mit dem Titelschlüssel aus ES_Decrypt (Task 9).

#include <string>
#include <vector>

#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl::platform {

inline constexpr u32 kChannelContent0Max = 4u * 1024 * 1024;

// Ganzer Content 0 (IMET + U8). Größe aus ISFS_GetFileStats, vor dem Allozieren gegen
// kChannelContent0Max geprüft. Fehler: NotFound (kein TMD/keine Datei), IoError, InvalidData (zu groß).
Result<std::vector<u8>> readChannelContent0(u64 titleId);

// opening.bnr (IMET bei 0x40) aus dem Abbild gamePath. Der Titelschlüssel wird im IOS entschlüsselt
// (ES_Decrypt, Schlüssel 4 bzw. 11); der Launcher kennt keinen Schlüssel. ES-Fehler → IoError „es“.
Result<std::vector<u8>> readWiiDiscBanner(FileSystem& fs, const std::string& gamePath);

}  // namespace nl::platform
