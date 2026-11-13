#pragma once
// Genres und Altersfreigaben aus meta.bin v2 (M17, Spec 10.3/10.5).

#include <array>

#include "core/types.hpp"

namespace nl {

// Hauptgenres der GameTDB (<genres><maingenre>), Bit i der Genre-Maske = kGenres[i].
// Reihenfolge ist Dateiformat: neue Genres nur am Ende anhängen, höchstens 32.
inline constexpr std::array<const char*, 14> kGenres = {
    "theme", "general", "action", "sports", "adventure", "music", "racing",
    "fighting", "simulation", "role-playing", "strategy", "traditional", "health", "others"};
static_assert(kGenres.size() <= 32, "Genre-Maske ist u32");

// ESRB/CERO werden beim Erzeugen von meta.bin auf ein Alter abgebildet.
enum class RatingSystem : u8 { None, Usk, Pegi, Esrb, Cero };
struct Rating {
    RatingSystem system = RatingSystem::None;
    u8 age = 0;
};

}  // namespace nl
