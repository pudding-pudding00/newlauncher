#pragma once
// index.bin (Spec 5.5): Ordner mit mtime und Spiele; Big-Endian.

#include <span>
#include <string>
#include <vector>

#include "core/console.hpp"
#include "core/result.hpp"

namespace nl {

inline constexpr u16 kIndexVersion = 1;

struct IndexFolder {
    std::string path;
    u32 mtime;
};

enum IndexFlags : u8 { kFlagTitleFromHeader = 1, kFlagSecondDisc = 2, kFlagSplitFile = 4, kFlagTitleFromMeta = 8 };

struct IndexGame {
    Console console;
    u8 flags;
    std::string key, title, path;
    u64 size;
};

struct IndexData {
    std::vector<IndexFolder> folders;
    std::vector<IndexGame> games;
};

std::vector<u8> writeIndex(const IndexData& data);
Result<IndexData> readIndex(std::span<const u8> file);

}  // namespace nl
