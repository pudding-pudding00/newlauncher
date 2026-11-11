#pragma once
// Baut ZIP-Archive für Host-Tests (Little Endian, ohne ZIP64). Deflate-Einträge nehmen fertige Proben aus
// deflate_samples.hpp; Felder lassen sich gezielt verfälschen.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/crc32.hpp"
#include "core/types.hpp"

namespace nl::test {

struct TestZipEntry {
    std::string name;
    std::vector<u8> data;  // so wie im Archiv (gespeichert oder komprimiert)
    u32 size = 0;          // entpackte Größe laut Verzeichnis
    u32 crc = 0;
    u16 method = 0;
    u16 flags = 0;
};

inline TestZipEntry storedEntry(std::string name, std::string_view text) {
    std::vector<u8> d(text.begin(), text.end());
    const u32 crc = crc32(d);
    return TestZipEntry{std::move(name), std::move(d), static_cast<u32>(text.size()), crc, 0, 0};
}

inline TestZipEntry dirEntry(std::string name) { return TestZipEntry{std::move(name), {}, 0, 0, 0, 0}; }

inline TestZipEntry deflatedEntry(std::string name, std::span<const u8> packed, std::size_t size, u32 crc) {
    return TestZipEntry{std::move(name), std::vector<u8>(packed.begin(), packed.end()), static_cast<u32>(size), crc,
                        8, 0};
}

inline void le16(std::vector<u8>& o, u32 v) {
    o.push_back(static_cast<u8>(v));
    o.push_back(static_cast<u8>(v >> 8));
}
inline void le32(std::vector<u8>& o, u32 v) {
    le16(o, v & 0xFFFFu);
    le16(o, v >> 16);
}

// comment: optionaler Archivkommentar hinter dem End-of-Central-Directory.
inline std::vector<u8> buildZip(const std::vector<TestZipEntry>& entries, std::string_view comment = {}) {
    std::vector<u8> out, central;
    for (const TestZipEntry& e : entries) {
        const u32 offset = static_cast<u32>(out.size());
        le32(out, 0x04034b50u);
        le16(out, 20);
        le16(out, e.flags);
        le16(out, e.method);
        le32(out, 0);  // Zeit/Datum
        le32(out, e.crc);
        le32(out, static_cast<u32>(e.data.size()));
        le32(out, e.size);
        le16(out, static_cast<u32>(e.name.size()));
        le16(out, 4);  // Extra-Feld (wird übersprungen)
        out.insert(out.end(), e.name.begin(), e.name.end());
        le32(out, 0xCAFE0000u);
        out.insert(out.end(), e.data.begin(), e.data.end());

        le32(central, 0x02014b50u);
        le16(central, 20);
        le16(central, 20);
        le16(central, e.flags);
        le16(central, e.method);
        le32(central, 0);
        le32(central, e.crc);
        le32(central, static_cast<u32>(e.data.size()));
        le32(central, e.size);
        le16(central, static_cast<u32>(e.name.size()));
        le16(central, 0);
        le16(central, 0);
        le16(central, 0);
        le16(central, 0);
        le32(central, 0);
        le32(central, offset);
        central.insert(central.end(), e.name.begin(), e.name.end());
    }
    const u32 cdOffset = static_cast<u32>(out.size());
    out.insert(out.end(), central.begin(), central.end());
    le32(out, 0x06054b50u);
    le16(out, 0);
    le16(out, 0);
    le16(out, static_cast<u32>(entries.size()));
    le16(out, static_cast<u32>(entries.size()));
    le32(out, static_cast<u32>(central.size()));
    le32(out, cdOffset);
    le16(out, static_cast<u32>(comment.size()));
    out.insert(out.end(), comment.begin(), comment.end());
    return out;
}

}  // namespace nl::test
