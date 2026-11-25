#pragma once
// Konsolentabelle (Spec 5.3). Die enum-Reihenfolge ist die Anzeige-Reihenfolge:
// nach Generation, innerhalb einer Generation Heimkonsole vor Handheld.

#include <array>

#include "core/types.hpp"

namespace nl {

enum class Console : u8 { Nes, Snes, Gb, N64, Gbc, Gc, Gba, Wii, Channel };
inline constexpr int kConsoleCount = 9;

struct ConsoleInfo {
    Console id;
    const char* key;    // Kürzel aus Spec 5.3, z. B. "n64"
    const char* label;  // kurze Anzeige, z. B. "N64"
    const char* name;   // voller Name, z. B. "Nintendo 64"
    u8 generation;
    bool handheld;
    std::array<const char*, 2> folders;     // Standardordner relativ zum Quellgerät (Spec v2 5.3); nullptr = unbenutzt
    std::array<const char*, 5> extensions;  // Standardendungen, klein, mit Punkt; nullptr = unbenutzt
};

const ConsoleInfo& consoleInfo(Console c);

// M23: typisches Seitenverhältnis (Breite/Höhe) der Cover einer Konsole, gemessen an libretro-Boxarts
// (SNES/N64 512×357, GB/GBC/GBA 512×512, NES ~359×512) und GameTDB (Wii/GC 5:7). Bestimmt die Kachelhöhe.
constexpr float coverAspect(Console c) {
    switch (c) {
        case Console::Snes:
        case Console::N64: return 10.0f / 7.0f;
        case Console::Gb:
        case Console::Gbc:
        case Console::Gba: return 1.0f;
        default: return 5.0f / 7.0f;
    }
}

}  // namespace nl
