#pragma once
// cIOS-Slots 200–255 einordnen und den Slot für ein Wii-Spiel wählen (Spec v2 6.2, M13 Task 2).

#include "core/ios_info.hpp"
#include "core/types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nl {

inline constexpr u8 kCiosFirstSlot = 200;
inline constexpr u8 kCiosLastSlot = 255;
inline constexpr u16 kStubTitleVersion = 0xFF00;

// D2x: ios_info_t-Magic; Stub: Titelversion 0xFF00 oder nur ein Content; Other: installiert, nicht einzuordnen.
enum class CiosKind : u8 { D2x, Stub, Other };

struct CiosEntry {
    u8 slot = 0;
    CiosKind kind = CiosKind::Other;
    u32 base = kDefaultCiosBase;  // D2x: baseios; sonst 56
    u16 titleVersion = 0;
    std::string name;     // D2x: ios_info_t-Name
    std::string version;  // D2x: versionstring (für NL_CIOS und Systeminfo)
};

CiosKind classifySlot(u16 titleVersion, u32 contentCount, const std::optional<IosInfo>& info);
const char* ciosKindName(CiosKind k);  // "d2x", "stub", "other"

// forced: der Slot stammt aus games.ini oder config.ini; dann gibt es keinen Rückfall auf die Automatik.
struct CiosChoice {
    std::optional<u8> slot;
    bool forced = false;
};

// Vorrang: gameIos > configIos > erster d2x in 249, 250, 248 > d2x mit höchstem Slot > 249 mit Other > nichts.
CiosChoice chooseCios(std::span<const CiosEntry> found, std::optional<u8> gameIos, std::optional<u8> configIos);

// "auto" oder leer → nullopt; "200".."255" → Wert; sonst nullopt. isBadIosValue meldet den letzten Fall (NL_CFG_BAD).
std::optional<u8> parseIosValue(std::string_view v);
bool isBadIosValue(std::string_view v);

// Brücke zum Preflight: kein Slot, Slot fehlt oder Stub → Missing (mit Slot); D2x → D2x; Other → Unverified.
CiosStatus ciosStatusFor(std::span<const CiosEntry> found, const CiosChoice& choice);

}  // namespace nl
