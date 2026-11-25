#pragma once
// M20 (Spec 4.2): Parameterblock für den Rückkehr-Stub bei 0x80002800 bauen. Format und Grenzen teilt er mit
// der Prüf-Logik des Stubs (src/launch/return_stub/stub_check.h).

#include "core/fat_chain.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace nl {

// Adresse, an der der argv-Block im Parameterblock liegt (packArgvBlock braucht sie als baseAddr).
inline constexpr u32 kReturnArgvAddr = 0x80002818;

enum class ReturnStubError : u8 { NoExtents, NotContiguous, SectorMismatch, DolSize, ArgvTooLarge, TooManyExtents };

// Extents in Dateireihenfolge, lückenlos, mindestens ceil(dolSize / 512) Sektoren (z. B. ganze Cluster aus
// fileExtents); der Block enthält genau so viele.
Result<std::vector<u8>, ReturnStubError> buildReturnParams(u32 dolSize, std::span<const Extent> extents,
                                                           std::span<const u8> argvBlock);

// M21: Argument, mit dem der Stub newlauncher nach einem Wii-Spiel startet (IOS 58 wiederherstellen).
inline constexpr std::string_view kFromGameFlag = "--from-game";

// M21: Stub vor einem Wii-Spiel/Kanal nur, wenn newlauncher von sd:/ läuft (der Stub liest die SD) und keine
// Cheats aktiv sind (der Codehandler des Booters belegt 0x80001800…0x80003000).
bool gameReturnStubWanted(std::string_view selfDol, std::size_t gctBytes);

}  // namespace nl
