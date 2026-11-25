#include "core/self_update.hpp"

#include <vector>

#include "core/ini.hpp"

namespace nl {

namespace {

std::optional<std::vector<u32>> versionParts(std::string_view v) {
    std::vector<u32> parts;
    if (v.empty()) return std::nullopt;
    std::size_t i = 0;
    while (true) {
        u32 n = 0;
        std::size_t digits = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
            if (n > 100000) return std::nullopt;
            n = n * 10 + static_cast<u32>(v[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0) return std::nullopt;
        parts.push_back(n);
        if (i == v.size()) return parts;
        if (v[i] != '.') return std::nullopt;
        ++i;
    }
}

std::optional<u32> parseU32(std::string_view s) {
    if (s.empty() || s.size() > 10) return std::nullopt;
    u64 n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + static_cast<u64>(c - '0');
    }
    if (n > 0xFFFFFFFFu) return std::nullopt;
    return static_cast<u32>(n);
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

}  // namespace

bool versionNewer(std::string_view candidate, std::string_view current) {
    const auto a = versionParts(candidate), b = versionParts(current);
    if (!a || !b) return false;
    const std::size_t n = a->size() > b->size() ? a->size() : b->size();
    for (std::size_t i = 0; i < n; ++i) {
        const u32 x = i < a->size() ? (*a)[i] : 0, y = i < b->size() ? (*b)[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

std::optional<UpdateManifest> parseUpdateManifest(std::string_view text, std::string_view manifestUrl) {
    if (!startsWith(manifestUrl, "https://")) return std::nullopt;
    const IniDoc ini = IniDoc::parse(text);
    UpdateManifest m;
    m.version = std::string(ini.get("update", "version"));
    if (!versionParts(m.version)) return std::nullopt;
    const auto zipSize = parseU32(ini.get("update", "zip_size"));
    const auto unzipped = parseU32(ini.get("update", "unzipped_size"));
    if (!zipSize || !unzipped || *zipSize == 0 || *zipSize > kMaxUpdateZipBytes) return std::nullopt;
    m.zipSize = *zipSize;
    m.unzippedSize = *unzipped;

    const std::string_view zip = ini.get("update", "zip");
    if (zip.empty()) return std::nullopt;
    if (startsWith(zip, "https://")) {
        m.zipUrl = std::string(zip);
    } else {
        if (zip.find("://") != std::string_view::npos || zip.find('/') != std::string_view::npos ||
            zip.find('\\') != std::string_view::npos || zip == "." || zip == "..")
            return std::nullopt;
        const std::size_t slash = manifestUrl.rfind('/');
        m.zipUrl = std::string(manifestUrl.substr(0, slash + 1)) + std::string(zip);
    }
    return m;
}

OscApp selfUpdateApp(const UpdateManifest& m) {
    OscApp a;
    a.name = "newlauncher";
    a.zipSize = m.zipSize;
    a.unzippedSize = m.unzippedSize;
    a.displayName = "newlauncher";
    a.version = m.version;
    return a;
}

}  // namespace nl
