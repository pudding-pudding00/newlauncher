#pragma once
// Disc-Header von Wii- und GC-Abbildern (Spec 6.2): ID6 und interner Titel.

#include <optional>
#include <span>
#include <string>

#include "core/types.hpp"

namespace nl {

struct DiscInfo {
    char id6[7];
    std::string title;  // UTF-8 (Header-Bytes werden als Latin-1 gelesen)
};

inline constexpr std::size_t kDiscProbeBytes = 1024;  // reicht für alle drei Formate bei hd_sec_sz_s == 9

std::optional<DiscInfo> parseWiiIso(std::span<const u8> first256);  // Magic 0x5D1C9EA3 @0x18
std::optional<DiscInfo> parseGcIso(std::span<const u8> first256);   // Magic 0xC2339F3D @0x1C
std::optional<DiscInfo> parseWbfs(std::span<const u8> first1024);   // "WBFS" @0, Disc-Header @ (1 << hd_sec_sz_s)

}  // namespace nl
