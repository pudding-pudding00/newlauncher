#pragma once
// M15: WIP-Patches (<data>/codes/<ID6>.wip, Spec v2 8). Format wie WiiFlow source/loader/wip.c: je Zeile
// "offset original neu" in Hex, "#" beginnt einen Kommentar. Der Game-Booter (boot/source/wip.c do_wip_code)
// vergleicht offset mit der Position im Datenstrom der main.dol-Sektionen, nicht mit einer 0x80…-Adresse,
// und ersetzt Byte für Byte nur, wo das Original passt.

#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kWipMaxEntries = 1024;    // = NL_WIP_MAX im Booter-Übergabeformat
inline constexpr u32 kWipMaxOffset = 0x01800000u;      // 24 MiB MEM1: größer kann main.dol nicht sein
inline constexpr std::size_t kWipMaxBadLines = 16;     // so viele Zeilennummern fürs Log
inline constexpr std::size_t kMaxWipBytes = 128 * 1024;

struct WipEntry {
    u32 offset = 0, original = 0, replacement = 0;
};

struct WipFile {
    std::vector<WipEntry> entries;  // höchstens kWipMaxEntries
    u32 skipped = 0;                // kaputte Zeilen und Einträge über der Grenze
    std::vector<u32> badLines;      // 1-basierte Zeilennummern übersprungener Zeilen, höchstens kWipMaxBadLines
};

// Gültig: genau drei Felder aus 1–8 Hex-Ziffern (optional "0x"), offset < kWipMaxOffset. Leer- und
// Kommentarzeilen zählen nicht; alles andere Unpassende zählt als übersprungen.
WipFile parseWip(std::string_view text);

std::string wipPath(std::string_view dataDir, std::string_view id6);  // <data>/codes/<ID6>.wip

}  // namespace nl
