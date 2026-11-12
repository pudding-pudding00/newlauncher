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
bool isChannelTitle(u64 titleId);  // obere 32 Bit == kChannelTitleType

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

inline constexpr std::size_t kImetReadBytes = 0x600;  // so viel liest platform/ vom Content 0

// Name in `lang`, sonst Englisch, sonst der erste nicht leere; "" ohne IMET oder bei abgeschnittenen Daten.
std::string imetChannelName(std::span<const u8> content0, BannerLang lang);

}  // namespace nl
