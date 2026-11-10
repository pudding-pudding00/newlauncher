#pragma once
// PNG → RGBA8 über libpng (Spec 6.4). Palette, Graustufen, 16 Bit und Interlace werden umgewandelt.
// libpng meldet Fehler per longjmp: hier abgefangen, nie als Exception.

#include <span>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl::platform {

inline constexpr u32 kMaxPngSide = 4096;
inline constexpr u32 kMaxPngPixels = 4u * 1024 * 1024;  // 16 MiB RGBA: mehr verträgt der Wii-Speicher nicht

Result<std::vector<u8>> decodePng(std::span<const u8> file, u32& width, u32& height);

}  // namespace nl::platform
