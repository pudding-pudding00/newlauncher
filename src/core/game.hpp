#pragma once
// Ein Spiel, wie die UI es sieht. In M1 aus Testdaten, ab M2 vom Scanner.

#include "core/console.hpp"
#include "core/rating.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u8 kNoDevice = 0xFF;  // virtuelle Einträge ohne Bibliotheksgerät (eingelegte Disc, EmuNAND-Kanal)

struct Game {
    Console console;
    const char* key;    // Spiel-Schlüssel (Spec 5.2)
    const char* title;  // UTF-8
    u16 year;           // 0 = unbekannt
    u8 players;         // 0 = unbekannt
    const char* description = nullptr;  // aus meta.bin, sonst nullptr
    const char* path = nullptr;         // relativ zum Bibliotheksgerät
    u64 size = 0;
    u8 flags = 0;                       // IndexFlags
    u8 device = 0;                      // Index in die Geräteliste der Sitzung (LibraryStore::devices)
    u32 genres = 0;                     // Bitmaske über kGenres, aus meta.bin v2 (M17)
    Rating rating{};                    // Altersfreigabe, aus meta.bin v2 (M17)
};

}  // namespace nl
