#pragma once
// Klassifizierung einer eingelegten optischen Disc (M19, Spec v2 12.1): Wii, GameCube oder unbekannt.

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

enum class DiscKind : u8 { None, Wii, Gc, Unknown };

struct OpticalDisc {
    DiscKind kind = DiscKind::None;
    std::string id6;
    std::string title;
};

// Prüft den Anfang der Disc (vollständiger 0x440-Header aus DI_UNENCREAD/READ oder 0x20-Byte-Kopf aus DI_READID).
// Wii-Magic 0x5D1C9EA3 @0x18, GC-Magic 0xC2339F3D @0x1C.
OpticalDisc classifyDisc(std::span<const u8> header);

// Für den Smoke-Test ([debug] fake_disc = <ID6>|gc:<ID6>): liefert OpticalDisc oder nullopt bei ungültiger Angabe.
std::optional<OpticalDisc> parseFakeDisc(std::string_view spec);

}  // namespace nl
