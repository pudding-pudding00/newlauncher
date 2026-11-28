#pragma once
// 32-bit Big-Endian PowerPC ELF validator and in-memory DOL converter (M31).

#include <span>
#include <vector>

#include "core/launch_layout.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

bool isElfMagic(std::span<const u8> data);

// Validates a 32-bit BE PowerPC ET_EXEC ELF and converts its PT_LOAD segments into a valid DOL image.
// The resulting DOL is verified with checkDolImage(out, maxDolBytes).
Result<std::vector<u8>> elfToDol(std::span<const u8> elf, u32 maxDolBytes = kMaxDolBytes);

}  // namespace nl
