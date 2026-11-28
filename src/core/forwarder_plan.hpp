#pragma once
// Forwarder DOL candidate paths, argv helpers, and Priiloader "Pune" magic constants (M30).

#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr std::string_view kForwarderFlag = "--from-forwarder";
inline constexpr u32 kPriiloaderMagicAddr = 0x8132FFFB;
inline constexpr u32 kPriiloaderSystemMenuMagic = 0x50756E65u;  // "Pune"

std::vector<std::string> forwarderCandidates();
std::vector<std::string> forwarderArgv(std::string_view dolPath);
bool hasForwarderFlag(int argc, const char* const* argv);

}  // namespace nl
