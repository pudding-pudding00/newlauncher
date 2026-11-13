#include "core/wip_patch.hpp"

#include <optional>

#include "core/fs.hpp"

namespace nl {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::optional<u32> parseHexField(std::string_view s) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);
    if (s.empty() || s.size() > 8) return std::nullopt;
    u32 v = 0;
    for (char c : s) {
        u32 d = 0;
        if (c >= '0' && c <= '9') d = static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<u32>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<u32>(c - 'A' + 10);
        else return std::nullopt;
        v = (v << 4) | d;
    }
    return v;
}

// Teilt die Zeile (ohne Kommentar) an Leerraum; mehr als 3 Felder zählen als 4.
std::size_t splitFields(std::string_view line, std::string_view (&out)[3]) {
    std::size_t n = 0, i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        std::size_t j = i;
        while (j < line.size() && !isSpace(line[j])) ++j;
        if (n < 3) out[n] = line.substr(i, j - i);
        ++n;
        i = j;
    }
    return n;
}

}  // namespace

WipFile parseWip(std::string_view text) {
    WipFile f;
    u32 lineNo = 0;
    auto skip = [&] {
        ++f.skipped;
        if (f.badLines.size() < kWipMaxBadLines) f.badLines.push_back(lineNo);
    };
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        std::string_view fields[3];
        const std::size_t n = splitFields(line, fields);
        if (n == 0) continue;
        if (n != 3) { skip(); continue; }
        const auto off = parseHexField(fields[0]);
        const auto orig = parseHexField(fields[1]);
        const auto repl = parseHexField(fields[2]);
        if (!off || !orig || !repl || *off >= kWipMaxOffset) { skip(); continue; }
        if (f.entries.size() >= kWipMaxEntries) { skip(); continue; }
        f.entries.push_back({*off, *orig, *repl});
    }
    return f;
}

std::string wipPath(std::string_view dataDir, std::string_view id6) {
    return joinPath(joinPath(dataDir, "codes"), std::string(id6) + ".wip");
}

}  // namespace nl
