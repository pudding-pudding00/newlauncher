#pragma once
// NAND-Titel (Spec 6.7): TMD lesen, um den Banner-Content zu finden, und den Kanalnamen aus dem
// IMET-Header des Banners. Reine Byte-Auswertung, plattformfrei; das Lesen aus dem NAND macht platform/.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kChannelTitleType = 0x00010001;
inline constexpr u32 kSelfChannelLow = 0x554E4C41u;  // "UNLA"
inline constexpr u64 kSelfChannelTitleId = 0x00010001554E4C41ull;
bool isChannelTitle(u64 titleId);  // obere 32 Bit == kChannelTitleType
inline bool isSelfChannelTitle(u64 titleId) { return titleId == kSelfChannelTitleId; }

struct TmdContent {
    u32 id = 0;
    u16 index = 0;
    u16 type = 0;
    u64 size = 0;
};

struct TmdInfo {
    u64 titleId = 0;
    u16 titleVersion = 0;  // 0x1DC; cIOS-Stubs: 0xFF00
    u16 bootIndex = 0;
    std::vector<TmdContent> contents;
};

// TMD mit RSA-2048-Signatur: Title-ID 0x18C, Titelversion 0x1DC, Anzahl 0x1DE, Boot-Index 0x1E0, Contents ab 0x1E4 je 36 Byte.
Result<TmdInfo> parseTmd(std::span<const u8> data);
std::optional<u32> contentIdForIndex(const TmdInfo& tmd, u16 index);

// Reihenfolge der Namen im IMET-Header (danach folgen Chinesisch ×2 und Koreanisch).
enum class BannerLang : u8 { Japanese, English, German, French, Spanish, Italian, Dutch };

inline constexpr std::size_t kImetReadBytes = 0x640;  // reicht für IMET bei 0x40 und 0x80 (Kanal-Content 0)

// Name in `lang`, sonst Englisch, sonst der erste nicht leere; "" ohne IMET oder bei abgeschnittenen Daten.
std::string imetChannelName(std::span<const u8> content0, BannerLang lang);

// Offset des U8-Archivs hinter dem IMET-Kopf: "IMET" bei 0x40 (Disc-opening.bnr, Kanal) → 0x600,
// bei 0x80 (Kanal-Content mit 0x40 Byte Build-Tag davor) → 0x640; sonst leer.
std::optional<u32> imetArchiveOffset(std::span<const u8> content0);

}  // namespace nl
