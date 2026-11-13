#pragma once
// U8-Archiv (Magic 55 AA 38 2D) aus Kanal- und Disc-Bannern (M18). Die Daten gelten als nicht
// vertrauenswürdig: jeder Knoten, Name und Datenbereich wird beim Parsen geprüft.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kU8MaxNodes = 4096;

class U8Archive {
public:
    // Hält nur eine Sicht auf `data`; die Bytes müssen länger leben als das Archiv.
    static Result<U8Archive> parse(std::span<const u8> data);
    // Pfad relativ zur Wurzel, mit '/' getrennt, Groß/klein egal ("meta/banner.bin").
    std::optional<std::span<const u8>> file(std::string_view path) const;

private:
    struct Entry {
        std::string path;  // klein geschrieben
        u32 offset = 0;
        u32 size = 0;
    };
    std::span<const u8> data_;
    std::vector<Entry> files_;
};

}  // namespace nl
