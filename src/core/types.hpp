#pragma once
// Kurze Ganzzahltypen im Stil von libogc, aber plattformfrei im Namespace nl.

#include <cstddef>
#include <cstdint>

namespace nl {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s32 = std::int32_t;
}  // namespace nl
