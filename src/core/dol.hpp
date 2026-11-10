#pragma once
// DOL-Header (Wii/GC-Programmformat): 7 Text- und 11 Datensektionen, BSS und Einsprung.
// parseDol prüft, dass jede Sektion in der Datei liegt und ihr Ziel weder außerhalb von MEM1/MEM2
// liegt noch den reservierten Chainloader-Bereich (core/launch_layout.hpp) überschreibt.

#include <array>
#include <span>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kDolHeaderBytes = 0x100;
inline constexpr std::size_t kDolTextSections = 7;
inline constexpr std::size_t kDolSections = 18;  // 0–6 Text, 7–17 Daten

struct DolSection {
    u32 offset = 0;
    u32 address = 0;
    u32 size = 0;
};

struct DolHeader {
    std::array<DolSection, kDolSections> sections{};
    u32 bssAddress = 0;
    u32 bssSize = 0;
    u32 entry = 0;
};

// head enthält mindestens die ersten kDolHeaderBytes der Datei; fileSize ist die ganze Dateigröße.
Result<DolHeader> parseDol(std::span<const u8> head, u64 fileSize);

}  // namespace nl
