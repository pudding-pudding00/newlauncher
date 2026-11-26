#include "core/nand_path.hpp"

#include <cstdio>
#include <span>
#include <vector>

#include "core/crc32.hpp"
#include "core/types.hpp"

namespace nl {
namespace {

constexpr std::string_view kNandPrefix = "nand:";

bool isHex8(std::string_view s) {
    if (s.size() != 8) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

// "/a/b/c" → {"a","b","c"}; nullopt bei leeren Teilen, ".", ".." oder zu langen Namen.
std::optional<std::vector<std::string_view>> splitIsfs(std::string_view p) {
    if (p.empty() || p.front() != '/' || p.size() > kIsfsMaxPath) return std::nullopt;
    std::vector<std::string_view> parts;
    std::size_t start = 1;
    while (start <= p.size()) {
        const std::size_t end = std::min(p.find('/', start), p.size());
        const std::string_view part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.size() > kIsfsMaxName) return std::nullopt;
        parts.push_back(part);
        start = end + 1;
    }
    return parts;
}

// Anzahl der Teile unterhalb von /title/<hi>/<lo>/data, oder -1, wenn der Pfad nicht dort liegt.
int depthBelowData(std::string_view isfs) {
    const auto parts = splitIsfs(isfs);
    if (!parts || parts->size() < 4) return -1;
    const auto& v = *parts;
    if (v[0] != "title" || !isHex8(v[1]) || !isHex8(v[2]) || v[3] != "data") return -1;
    return static_cast<int>(v.size()) - 4;
}

}  // namespace

std::optional<std::string> isfsPath(std::string_view path) {
    if (path.substr(0, kNandPrefix.size()) != kNandPrefix) return std::nullopt;
    const std::string_view rest = path.substr(kNandPrefix.size());
    if (rest.empty() || rest.front() != '/') return std::nullopt;
    if (rest != "/" && !splitIsfs(rest)) return std::nullopt;
    return std::string(rest);
}

bool isSaveDataPath(std::string_view isfs) { return depthBelowData(isfs) >= 1; }

bool isSaveDataDir(std::string_view isfs) { return depthBelowData(isfs) >= 0; }

std::string nandTempPath(std::string_view isfs) {
    const u32 c = crc32(std::span<const u8>(reinterpret_cast<const u8*>(isfs.data()), isfs.size()));
    char buf[24];
    std::snprintf(buf, sizeof buf, "/tmp/nl_%08x", static_cast<unsigned>(c));
    return buf;
}

}  // namespace nl
