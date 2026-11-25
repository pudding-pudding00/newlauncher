#pragma once
// M20 (Spec 4.2): Parameterblock für den Rückkehr-Stub bei 0x80002800 bauen. Format und Grenzen teilt er mit
// der Prüf-Logik des Stubs (src/launch/return_stub/stub_check.h).

#include "core/fat_chain.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace nl {

// Adresse, an der der argv-Block im Parameterblock liegt (packArgvBlock braucht sie als baseAddr).
inline constexpr u32 kReturnArgvAddr = 0x80002818;

enum class ReturnStubError : u8 { NoExtents, NotContiguous, SectorMismatch, DolSize, ArgvTooLarge, TooManyExtents };

// Extents in Dateireihenfolge, lückenlos; sie müssen genau ceil(dolSize / 512) Sektoren abdecken.
Result<std::vector<u8>, ReturnStubError> buildReturnParams(u32 dolSize, std::span<const Extent> extents,
                                                           std::span<const u8> argvBlock);

}  // namespace nl
