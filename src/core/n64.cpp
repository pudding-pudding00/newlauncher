#include "core/n64.hpp"

namespace nl {

namespace {

constexpr const char* kSection = "n64_method";

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

}  // namespace

const char* n64MethodKey(N64Method m) {
    switch (m) {
        case N64Method::Vc: return "vc";
        case N64Method::Not64: return "not64";
        case N64Method::Wii64: return "wii64";
    }
    return "vc";
}

const char* n64MethodLabel(N64Method m) {
    switch (m) {
        case N64Method::Vc: return "VC-Inject";
        case N64Method::Not64: return "Not64";
        case N64Method::Wii64: return "Wii64";
    }
    return "VC-Inject";
}

std::optional<N64Method> parseN64Method(std::string_view s) {
    s = trim(s);
    for (N64Method m : {N64Method::Vc, N64Method::Not64, N64Method::Wii64})
        if (equalsIgnoreCase(s, n64MethodKey(m))) return m;
    return std::nullopt;
}

GameOverrides GameOverrides::fromIni(const IniDoc& doc) {
    GameOverrides o;
    for (const auto& [k, v] : doc.entries(kSection))
        if (const auto m = parseN64Method(v)) o.setN64Method(k, *m);
    return o;
}

std::optional<N64Method> GameOverrides::n64Method(std::string_view key) const {
    for (const auto& [k, m] : n64_)
        if (k == key) return m;
    return std::nullopt;
}

void GameOverrides::setN64Method(std::string_view key, std::optional<N64Method> m) {
    for (auto it = n64_.begin(); it != n64_.end(); ++it) {
        if (it->first != key) continue;
        if (m) it->second = *m;
        else n64_.erase(it);
        return;
    }
    if (m) n64_.emplace_back(std::string(key), *m);
}

IniDoc GameOverrides::toIni() const {
    // Neu aus Text aufgebaut, weil IniDoc kein Entfernen kennt.
    std::string text = std::string("[") + kSection + "]\n";
    for (const auto& [k, m] : n64_) text += k + " = " + n64MethodKey(m) + "\n";
    return IniDoc::parse(text);
}

}  // namespace nl
