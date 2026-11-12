#pragma once
// Ein Spiel, wie die UI es sieht. In M1 aus Testdaten, ab M2 vom Scanner.

#include "core/console.hpp"
#include "core/types.hpp"

namespace nl {

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
};

}  // namespace nl
