#pragma once
// ZIP-Schreiber für Spielstand-Pakete (M26, Spec Ziel 2): alles im Speicher, Methoden 0 und 8, kein ZIP64.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

// MS-DOS-Zeit (Datum in den oberen 16 Bit) einer Unix-Zeit in UTC; vor 1980 → 1980-01-01.
u32 dosTimeFromUnix(u64 unix);

class ZipWriter {
public:
    // Unsicherer Name (isSafeZipPath), doppelter Name, mehr als kMaxZipEntries Einträge oder Daten über
    // kMaxZipEntryBytes → InvalidData. deflate = true komprimiert nur, wenn das Ergebnis kleiner ist.
    Result<void> add(std::string_view name, std::span<const u8> data, bool deflate, u32 dosTime);
    std::vector<u8> finish();  // danach nicht weiterverwenden

private:
    std::vector<u8> out_;
    std::vector<u8> central_;
    std::vector<std::string> names_;
};

}  // namespace nl
