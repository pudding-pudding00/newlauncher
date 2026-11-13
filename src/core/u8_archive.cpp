#include "core/u8_archive.hpp"

#include <algorithm>

namespace nl {

namespace {
u32 be32(std::span<const u8> b, std::size_t at) {
    return static_cast<u32>(b[at]) << 24 | static_cast<u32>(b[at + 1]) << 16 | static_cast<u32>(b[at + 2]) << 8 | b[at + 3];
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
Result<U8Archive> invalid(const char* what) { return Result<U8Archive>::err(Error{ErrorCode::InvalidData, what}); }
}  // namespace

Result<U8Archive> U8Archive::parse(std::span<const u8> data) {
    if (data.size() < 0x20 || be32(data, 0) != 0x55AA382Du) return invalid("U8: kein Archiv");
    const std::size_t root = be32(data, 4);
    if (root > data.size() || data.size() - root < 12) return invalid("U8: Wurzel");
    if ((data[root] & 0xFF) != 1) return invalid("U8: Wurzel ist kein Ordner");
    const u32 count = be32(data, root + 8);
    if (count == 0 || count > kU8MaxNodes) return invalid("U8: Knotenzahl");
    const std::size_t strings = root + std::size_t{count} * 12;
    if (strings > data.size()) return invalid("U8: Knoten hinter dem Ende");

    U8Archive a;
    a.data_ = data;
    struct Dir {
        u32 end;
        std::size_t prefix;  // Länge des Pfads samt '/'
    };
    std::vector<Dir> stack{{count, 0}};
    std::string path;
    // Nur vorwärts: jeder Ordner endet bei next (> i, ≤ Ende des Elternordners), daher keine Schleifen.
    for (u32 i = 1; i < count; ++i) {
        while (i >= stack.back().end) stack.pop_back();  // Wurzel endet bei count, wird nie entfernt
        path.resize(stack.back().prefix);
        const std::size_t e = root + std::size_t{i} * 12;
        const u32 w0 = be32(data, e);
        const u8 type = static_cast<u8>(w0 >> 24);
        if (type > 1) return invalid("U8: Knotentyp");
        const std::size_t nameAt = strings + (w0 & 0x00FFFFFFu);
        if (nameAt >= data.size()) return invalid("U8: Namensoffset");
        std::size_t end = nameAt;
        const std::size_t limit = std::min(data.size(), nameAt + kU8MaxName + 1);
        while (end < limit && data[end] != 0) ++end;
        if (end == data.size()) return invalid("U8: Name ohne Ende");
        if (end == limit) return invalid("U8: Name zu lang");
        if (path.size() + (end - nameAt) + 1 > kU8MaxPath) return invalid("U8: Pfad zu lang");
        if (end == nameAt) return invalid("U8: leerer Name");
        for (std::size_t k = nameAt; k < end; ++k) path += lower(static_cast<char>(data[k]));
        if (type == 1) {
            const u32 next = be32(data, e + 8);
            if (next <= i || next > stack.back().end) return invalid("U8: next");
            path += '/';
            stack.push_back(Dir{next, path.size()});
            continue;
        }
        const u32 off = be32(data, e + 4);
        const u32 size = be32(data, e + 8);
        if (off > data.size() || size > data.size() - off) return invalid("U8: Daten hinter dem Ende");
        a.files_.push_back(Entry{path, off, size});
    }
    return Result<U8Archive>::ok(std::move(a));
}

std::optional<std::span<const u8>> U8Archive::file(std::string_view path) const {
    for (const Entry& f : files_) {
        if (f.path.size() != path.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < path.size() && same; ++k) same = f.path[k] == lower(path[k]);
        if (same) return data_.subspan(f.offset, f.size);
    }
    return std::nullopt;
}

}  // namespace nl
