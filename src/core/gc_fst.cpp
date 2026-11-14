#include "core/gc_fst.hpp"

#include <string>

namespace nl {

namespace {
u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
Result<FstFile> invalid(const char* what) { return Result<FstFile>::err(Error{ErrorCode::InvalidData, what}); }
}  // namespace

Result<FstFile> findRootFile(std::span<const u8> fst, std::string_view name) {
    if (fst.size() < 12) return invalid("FST zu kurz");
    const u32 count = be32(fst, 8);
    if (count == 0 || count > fst.size() / 12) return invalid("FST: Anzahl");
    const std::size_t strings = std::size_t{count} * 12;
    // Nur vorwärts: jeder Schritt erhöht i, Ordner springen zu ihrem next (> i, ≤ count). Keine Schleife möglich.
    for (u32 i = 1; i < count;) {
        const std::size_t e = std::size_t{i} * 12;
        const u32 w0 = be32(fst, e);
        const bool dir = (w0 >> 24) != 0;
        const std::size_t nameAt = strings + (w0 & 0x00FFFFFFu);
        if (nameAt >= fst.size()) return invalid("FST: Namensoffset");
        std::size_t end = nameAt;
        while (end < fst.size() && fst[end] != 0) ++end;
        if (end == fst.size()) return invalid("FST: Name ohne Ende");
        if (dir) {
            const u32 next = be32(fst, e + 8);
            if (next <= i || next > count) return invalid("FST: next");
            i = next;
            continue;
        }
        if (end - nameAt == name.size()) {
            bool same = true;
            for (std::size_t k = 0; k < name.size() && same; ++k)
                same = lower(static_cast<char>(fst[nameAt + k])) == lower(name[k]);
            if (same) return Result<FstFile>::ok(FstFile{be32(fst, e + 4), be32(fst, e + 8)});
        }
        ++i;
    }
    return Result<FstFile>::err(Error{ErrorCode::NotFound, std::string(name)});
}

}  // namespace nl
