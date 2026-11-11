#pragma once
// cIOS in Slot 249 erkennen (M9 Task 4): iosinfo_t aus d2x (cios.h) lesen und einordnen.

#include "core/types.hpp"

#include <optional>
#include <span>
#include <string>

namespace nl {

inline constexpr u32 kIosInfoMagic = 0x1EE7C105;
inline constexpr u32 kIosInfoBytes = 0x30;  // magic, magicversion, version, baseios, name[16], versionstring[16]
inline constexpr u32 kDefaultCiosBase = 56;

struct IosInfo {
    u32 version = 0;
    u32 baseios = 0;
    std::string name;           // höchstens 16 Zeichen
    std::string versionString;  // höchstens 16 Zeichen
};

// Big-Endian wie im Modul; nullopt bei zu wenig Bytes, falscher Magic oder magicversion ≠ 1.
std::optional<IosInfo> parseIosInfo(std::span<const u8> first0x40);

enum class CiosState : u8 { Missing, NotD2x, D2x, Unverified };

struct CiosStatus {
    CiosState state = CiosState::Missing;
    u32 base = kDefaultCiosBase;
    std::string version;  // D2x: versionstring; NotD2x: name (Kontext für den Dialog)
};

// !tmdPresent → Missing; Info fehlt → Unverified mit Basis 56; name beginnt mit „d2x“ → D2x; sonst NotD2x.
CiosStatus classifyCios(bool tmdPresent, const std::optional<IosInfo>& info);

}  // namespace nl
