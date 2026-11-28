#pragma once
// wiiload v0.5 wire protocol parser, payload decoder (raw or zlib), argv splitter, and format detector.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u16 kWiiloadDefaultPort = 4299;
inline constexpr std::size_t kWiiloadHeaderBytes = 16;
inline constexpr u32 kWiiloadMaxCompBytes = 16u * 1024 * 1024;
inline constexpr u32 kWiiloadMaxUncompBytes = 16u * 1024 * 1024;
inline constexpr u16 kWiiloadMaxArgsBytes = 4096;
inline constexpr std::size_t kWiiloadMaxArgs = 64;

enum class WiiloadKind : u8 { Dol, Elf, Zip };

struct WiiloadHeader {
    u8 major = 0;
    u8 minor = 0;
    u16 argsLen = 0;
    u32 compSize = 0;
    u32 uncompSize = 0;  // 0 means payload is uncompressed
};

Result<WiiloadHeader> parseWiiloadHeader(std::span<const u8> hdr);

Result<std::vector<u8>> decodeWiiloadPayload(const WiiloadHeader& hdr, std::span<const u8> raw);

std::vector<std::string> parseWiiloadArgs(std::span<const u8> rawArgs);

WiiloadKind detectWiiloadKind(std::span<const u8> payload, std::string_view argv0 = {});

}  // namespace nl
