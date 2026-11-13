#include "core/cheat_select.hpp"

#include "core/gct.hpp"

#include <algorithm>

namespace nl {
namespace {

std::string_view trim(std::string_view s) {
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && sp(s.front())) s.remove_prefix(1);
    while (!s.empty() && sp(s.back())) s.remove_suffix(1);
    return s;
}

}  // namespace

std::vector<std::string> parseSel(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    std::vector<std::string> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        const std::string_view line = trim(text.substr(0, nl));
        if (!line.empty()) out.emplace_back(line);
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return out;
}

std::string writeSel(std::span<const std::string> names) {
    std::string out;
    for (const std::string& n : names) {
        out += n;
        out += '\n';
    }
    return out;
}

std::vector<const GeckoCode*> selectedCodes(const GeckoFile& f, std::span<const std::string> names) {
    std::vector<const GeckoCode*> out;
    std::vector<std::string_view> seen;
    for (const GeckoCode& c : f.codes) {
        if (std::find(seen.begin(), seen.end(), c.name) != seen.end()) continue;
        seen.push_back(c.name);
        if (c.hasPlaceholder) continue;
        if (std::find(names.begin(), names.end(), c.name) == names.end()) continue;
        out.push_back(&c);
    }
    return out;
}

std::vector<u8> gctForSelection(std::string_view txt, std::string_view sel) {
    auto file = parseGeckoTxt(txt);
    if (!file.isOk()) return {};
    const std::vector<std::string> names = parseSel(sel);
    const std::vector<const GeckoCode*> codes = selectedCodes(file.value(), names);
    if (codes.empty()) return {};
    return buildGct(codes);
}

}  // namespace nl
