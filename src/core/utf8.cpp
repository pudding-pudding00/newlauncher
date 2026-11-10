#include "core/utf8.hpp"

namespace nl {

u32 decodeUtf8(std::string_view s, std::size_t& pos) {
    if (pos >= s.size()) {
        ++pos;
        return '?';
    }
    const auto byte = [&](std::size_t i) { return static_cast<u8>(s[i]); };
    const u8 lead = byte(pos);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }
    int extra;
    u32 cp, minCp;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1; cp = lead & 0x1F; minCp = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2; cp = lead & 0x0F; minCp = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3; cp = lead & 0x07; minCp = 0x10000;
    } else {
        ++pos;  // Folgebyte ohne Startbyte oder 0xF8–0xFF
        return '?';
    }
    for (int i = 1; i <= extra; ++i) {
        const std::size_t at = pos + static_cast<std::size_t>(i);
        if (at >= s.size() || (byte(at) & 0xC0) != 0x80) {
            ++pos;  // nur das Startbyte verwerfen, der Rest wird neu gelesen
            return '?';
        }
        cp = (cp << 6) | (byte(at) & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;
        return '?';
    }
    pos += static_cast<std::size_t>(extra) + 1;
    return cp;
}

}  // namespace nl
