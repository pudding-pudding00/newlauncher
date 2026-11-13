#pragma once
// meta.bin (Spec 5.5/6.3): vom PC-Skript erzeugt, nach Schlüssel sortiert → binäre Suche.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/index_file.hpp"
#include "core/result.hpp"

namespace nl {

// Hauptgenres der GameTDB (<genres><maingenre>), Bit i der Genre-Maske = kGenres[i].
// Reihenfolge ist Dateiformat: neue Genres nur am Ende anhängen, höchstens 32.
inline constexpr std::array<const char*, 14> kGenres = {
    "theme", "general", "action", "sports", "adventure", "music", "racing",
    "fighting", "simulation", "role-playing", "strategy", "traditional", "health", "others"};
static_assert(kGenres.size() <= 32, "Genre-Maske ist u32");

// Altersfreigabe (M17); ESRB/CERO werden beim Erzeugen von meta.bin auf ein Alter abgebildet.
enum class RatingSystem : u8 { None, Usk, Pegi, Esrb, Cero };
struct Rating {
    RatingSystem system = RatingSystem::None;
    u8 age = 0;
};

struct MetaEntry {
    std::string_view title;
    u16 year;
    u8 players;
    std::string_view description;
    u32 genres = 0;  // Bitmaske über kGenres (erst ab Version 2)
    Rating rating;   // erst ab Version 2
};

class MetaDb {
public:
    // Magic "NLMD", Version 1 oder 2 (v2: je Eintrag u32 Genres, u8 System, u8 Alter); Schlüssel müssen streng aufsteigend sortiert sein (Bytevergleich).
    static Result<MetaDb> parse(std::vector<u8> file);
    std::optional<MetaEntry> find(std::string_view key) const;
    u32 size() const { return static_cast<u32>(offsets_.size()); }

private:
    std::vector<u8> file_;
    std::vector<u32> offsets_;  // Beginn jedes Eintrags in file_
    u16 version_ = 1;
};

// Metadaten-Schlüssel (Spec 5.2): ID6 bzw. "<konsole>/<Dateiname ohne Endung, ASCII kleingeschrieben>".
std::string metaKey(const IndexGame& g);

}  // namespace nl
