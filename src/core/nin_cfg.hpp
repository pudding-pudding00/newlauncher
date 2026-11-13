#pragma once
// NIN_CFG für Nintendont (Spec 6.5, Plan M8): 548-Byte-Block, Big-Endian, wie
// common/include/CommonConfig.h. Gesendet wird immer Version 10; neuere Nintendont heben ihn selbst an.

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kNinCfgSize = 548;
inline constexpr u32 kNinCfgMagic = 0x01070CF6;
inline constexpr u32 kNinCfgVersion = 10;
inline constexpr u32 kNinCfgMemcardEmu = 1u << 3;
inline constexpr u32 kNinCfgAutoBoot = 1u << 7;
inline constexpr u32 kNinCfgUsb = 1u << 10;
// Weitere Config-Bits (CommonConfig.h, Nintendont c683db3); seit 6.493 unverändert.
inline constexpr u32 kNinCfgCheats = 1u << 0;
inline constexpr u32 kNinCfgCheatPath = 1u << 4;
inline constexpr u32 kNinCfgForceWide = 1u << 5;
inline constexpr u32 kNinCfgForceProg = 1u << 6;
inline constexpr u32 kNinCfgMcMulti = 1u << 13;
inline constexpr u32 kNinCfgNativeSi = 1u << 14;
inline constexpr u32 kNinCfgWiiuWide = 1u << 15;
inline constexpr u32 kNinCfgSkipIpl = 1u << 18;
// VideoMode: Art in den oberen 16 Bit, Modus in den unteren.
inline constexpr u32 kNinVidAuto = 0;
inline constexpr u32 kNinVidForce = 1u << 16;
inline constexpr u32 kNinVidNone = 2u << 16;
inline constexpr u32 kNinVidPal50 = 1u << 0;
inline constexpr u32 kNinVidPal60 = 1u << 1;
inline constexpr u32 kNinVidNtsc = 1u << 2;
inline constexpr u32 kNinVidMpal = 1u << 3;
inline constexpr u32 kNinVidProg = 1u << 4;
// Language
inline constexpr u32 kNinLangAuto = 0xFFFFFFFFu;
inline constexpr u32 kNinLangEn = 0, kNinLangDe = 1, kNinLangFr = 2, kNinLangEs = 3, kNinLangIt = 4, kNinLangNl = 5;
inline constexpr std::size_t kNinMaxGamePath = 254;  // GamePath[255] mit NUL

struct NinConfigInput {
    std::string_view gamePath;  // mit führendem "/", relativ zum Bibliotheksgerät
    std::string_view id6;
    bool usb;
    u32 extraFlags = 0;               // zusätzliche Config-Bits (Spieloptionen)
    u32 videoMode = kNinVidAuto;
    u32 language = kNinLangAuto;
    u32 maxPads = 4;
    u32 memcardBlocks = 2;            // Index 0–5: (1 << (i + 6)) - 5 Blöcke, 2 = 251
    bool memcardEmu = true;           // false bei memcard = real (Spec 7.3)
};

enum class NinCfgError : u8 { PathTooLong, BadId };

Result<std::array<u8, kNinCfgSize>, NinCfgError> buildNinCfg(const NinConfigInput& in);

// Genau ein führendes "/", "\" wird zu "/".
std::string ninGamePath(std::string_view relPath);

// Version aus der Nintendont-DOL: ASCII "$$Version:<major>.<minor>" (Spec 10, Risiko NIN_CFG-Version).
struct NinVersion {
    u16 major = 0, minor = 0;
    auto operator<=>(const NinVersion&) const = default;
};
inline constexpr NinVersion kMinNintendont{6, 493};

// Erste Nintendont-Version, die eine GC-Option kennt (git log -S auf CommonConfig.h, Spec 15).
struct NinOptionGate {
    const char* key;
    NinVersion minVersion;
};
std::span<const NinOptionGate> ninOptionGates();

// Liest in Blöcken zu 64 KiB mit Überlappung; erstes "$$Version:" mit Ziffern.Ziffern, sonst nullopt.
std::optional<NinVersion> findNintendontVersion(FileSystem& fs, std::string_view dolPath);
std::string formatNinVersion(NinVersion v);  // "6.509"

}  // namespace nl
