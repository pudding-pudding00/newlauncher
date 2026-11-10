#pragma once
// Argumente für den Emulator-Start (Spec 5.4) und der libogc-argv-Block, den der App-Booter
// an die Ziel-DOL weiterreicht.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kArgvMagic = 0x5f617267;  // "_arg", libogc ARGV_MAGIC
inline constexpr u32 kArgvHeaderBytes = 24;
inline constexpr u32 kMaxArgvBytes = 0x800;

// argv[0] = dolPath, danach je ein Element pro `|`-Eintrag des Schemas mit {dir}/{file}/{path} ersetzt.
std::vector<std::string> expandArgs(std::string_view dolPath, std::string_view schema, std::string_view romPath);

// Header {magic, commandLine, length, argc=0, argv=0, endARGV=0} + Kommandozeile (NUL-getrennt, doppelt
// NUL-terminiert). baseAddr ist die Adresse, an der der Block im Speicher liegen wird.
Result<std::vector<u8>> packArgvBlock(std::span<const std::string> args, u32 baseAddr);

}  // namespace nl
