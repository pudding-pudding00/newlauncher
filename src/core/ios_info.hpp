#pragma once
// iosinfo_t aus d2x (cios.h) lesen (M9 Task 4); Einordnung und Slot-Wahl in core/cios_scan (M13).

#include "core/types.hpp"

#include <optional>
#include <span>
#include <string>

namespace nl {

inline constexpr u32 kIosInfoMagic = 0x1EE7C105;
inline constexpr u32 kIosInfoBytes = 0x30;  // magic, magicversion, version, baseios, name[16], versionstring[16]
inline constexpr u32 kDefaultCiosBase = 56;
inline constexpr u8 kDefaultCiosSlot = 249;  // Slot für Dialog und Dry-Run, wenn nichts gewählt ist

struct IosInfo {
    u32 version = 0;
    u32 baseios = 0;
    std::string name;           // höchstens 16 Zeichen
    std::string versionString;  // höchstens 16 Zeichen
};

// Big-Endian wie im Modul; nullopt bei zu wenig Bytes, falscher Magic oder magicversion ≠ 1.
std::optional<IosInfo> parseIosInfo(std::span<const u8> first0x40);

// Missing: kein brauchbares cIOS im gewählten Slot; Unverified: installiert, aber ohne ios_info_t (Spec 6.2 „Other“).
enum class CiosState : u8 { Missing, D2x, Unverified };

struct CiosStatus {
    CiosState state = CiosState::Missing;
    u32 base = kDefaultCiosBase;
    std::string version;  // D2x: versionstring
    u8 slot = kDefaultCiosSlot;
};

}  // namespace nl
