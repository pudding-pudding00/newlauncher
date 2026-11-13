#pragma once
// Liest opening.bnr eines GC-Spiels (M18) über das FileSystem des Spielgeräts: ISO/GCM, CISO
// (Blockkarte, leere Blöcke fehlen) und entpackte Abbilder (Ordner mit sys/boot.bin; Dateien unter root/
// wie bei Nintendont oder files/ wie bei Dolphin).

#include <string_view>

#include "core/fs.hpp"
#include "core/gc_banner.hpp"
#include "core/result.hpp"

namespace nl {

inline constexpr u32 kGcFstMaxBytes = 1024 * 1024;

// gamePath: vollständiger Pfad (Gerätewurzel + Game::path). Fehlt das Banner → NotFound.
Result<GcBanner> loadGcBanner(FileSystem& fs, std::string_view gamePath, u8 langIndex, bool shiftJis);

}  // namespace nl
