#pragma once
// NIN_CFG für Nintendont (Spec 6.5, Plan M8): 548-Byte-Block, Big-Endian, wie
// common/include/CommonConfig.h. Gesendet wird immer Version 10; neuere Nintendont heben ihn selbst an.

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kNinCfgSize = 548;
inline constexpr u32 kNinCfgMagic = 0x01070CF6;
inline constexpr u32 kNinCfgVersion = 10;
inline constexpr u32 kNinCfgMemcardEmu = 1u << 3;
inline constexpr u32 kNinCfgAutoBoot = 1u << 7;
inline constexpr u32 kNinCfgUsb = 1u << 10;
inline constexpr std::size_t kNinMaxGamePath = 254;  // GamePath[255] mit NUL

struct NinConfigInput {
    std::string_view gamePath;  // mit führendem "/", relativ zum Bibliotheksgerät
    std::string_view id6;
    bool usb;
};

enum class NinCfgError : u8 { PathTooLong, BadId };

Result<std::array<u8, kNinCfgSize>, NinCfgError> buildNinCfg(const NinConfigInput& in);

// Genau ein führendes "/", "\" wird zu "/".
std::string ninGamePath(std::string_view relPath);

}  // namespace nl
