#pragma once
// Auto-Wahl des N64-Startwegs (Spec 6.6) als reine Funktion: alle Fakten kommen fertig herein.

#include <array>
#include <optional>
#include <span>

#include "core/n64.hpp"
#include "core/types.hpp"

namespace nl {

enum class N64Unavailable : u8 { None, NotMapped, NotInstalled, DolMissing };

struct N64Inputs {
    std::optional<N64Method> override_;    // aus games.ini
    std::optional<u64> mappedTitle;        // aus n64_vc.ini
    std::span<const u64> installedTitles;  // Kanäle im NAND
    bool not64Dol = false;
    bool wii64Dol = false;
};

struct N64Decision {
    std::array<N64Unavailable, kN64MethodCount> why{};  // Index = N64Method
    std::optional<N64Method> chosen;
    bool overrideIgnored = false;  // Override gesetzt, aber nicht verfügbar
    bool mappingIgnored = false;   // Zuordnung gesetzt, Kanal nicht installiert
    bool available(N64Method m) const { return why[static_cast<std::size_t>(m)] == N64Unavailable::None; }
};

// Reihenfolge: verfügbarer Override → VC (zugeordnet und installiert) → Not64 → Wii64 → kein Weg.
N64Decision decideN64(const N64Inputs& in);

}  // namespace nl
