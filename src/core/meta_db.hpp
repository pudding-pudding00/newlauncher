#pragma once
// meta.bin (Spec 5.5/6.3): vom PC-Skript erzeugt, nach Schlüssel sortiert → binäre Suche.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/index_file.hpp"
#include "core/result.hpp"

namespace nl {

struct MetaEntry {
    std::string_view title;
    u16 year;
    u8 players;
    std::string_view description;
};

class MetaDb {
public:
    // Magic "NLMD", Version 1; Schlüssel müssen streng aufsteigend sortiert sein (Bytevergleich).
    static Result<MetaDb> parse(std::vector<u8> file);
    std::optional<MetaEntry> find(std::string_view key) const;
    u32 size() const { return static_cast<u32>(offsets_.size()); }

private:
    std::vector<u8> file_;
    std::vector<u32> offsets_;  // Beginn jedes Eintrags in file_
};

// Metadaten-Schlüssel (Spec 5.2): ID6 bzw. "<konsole>/<Dateiname ohne Endung, ASCII kleingeschrieben>".
std::string metaKey(const IndexGame& g);

}  // namespace nl
