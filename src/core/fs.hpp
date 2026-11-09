#pragma once
// Dateisystem-Schnittstelle (Spec 4.1): der Scanner und alle Dateiformate arbeiten nur hiergegen;
// auf der Wii steckt stdio dahinter, im Host-Test ein In-Memory-Dateisystem.
// Pfade enthalten das Gerät, z. B. "sd:/roms/nes"; Trenner ist immer '/'.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

struct DirEntry {
    std::string name;
    bool isDir;
    u64 size;
    u32 mtime;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual Result<std::vector<DirEntry>> list(std::string_view dir) = 0;  // ohne "." und ".."
    virtual Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) = 0;
    virtual Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) = 0;
    virtual Result<void> writeAtomic(std::string_view file, std::span<const u8> data) = 0;
    virtual Result<void> makeDirs(std::string_view dir) = 0;
};

inline std::string joinPath(std::string_view dir, std::string_view name) {
    std::string p(dir);
    if (!p.empty() && p.back() != '/') p += '/';
    p += name;
    return p;
}

}  // namespace nl
