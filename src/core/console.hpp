#pragma once
// Konsolentabelle (Spec 5.3). Die enum-Reihenfolge ist die Anzeige-Reihenfolge:
// nach Generation, innerhalb einer Generation Heimkonsole vor Handheld.

#include <array>

#include "core/types.hpp"

namespace nl {

enum class Console : u8 { Nes, Snes, Gb, N64, Gbc, Gc, Gba, Wii };
inline constexpr int kConsoleCount = 8;

struct ConsoleInfo {
    Console id;
    const char* key;    // Kürzel aus Spec 5.3, z. B. "n64"
    const char* label;  // kurze Anzeige, z. B. "N64"
    const char* name;   // voller Name, z. B. "Nintendo 64"
    u8 generation;
    bool handheld;
    const char* folder;                       // relativ zum Bibliotheksgerät (Spec 5.3)
    std::array<const char*, 3> extensions;    // klein, mit Punkt; nullptr = unbenutzt
};

const ConsoleInfo& consoleInfo(Console c);

}  // namespace nl
