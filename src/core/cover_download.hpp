#pragma once
// „Fehlende Cover laden“ (Spec 3.4, 6.4): Regionen, Aufträge, URL, Zusammenfassung.

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/game.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::string_view kGametdbCoverBase = "http://art.gametdb.com/wii/cover";
inline constexpr u32 kMaxCoverDownloadBytes = 2u * 1024 * 1024;

// Feste Region zuerst, sonst die Systemregion; danach EN, US, JA ohne Doppelte.
// Unbekannte Konfigurationswerte gelten als "auto".
std::vector<std::string_view> coverRegions(std::string_view configRegion, std::string_view systemRegion);

std::string gametdbCoverUrl(std::string_view base, std::string_view region, std::string_view id6);

struct CoverJob {
    std::string id6;
    std::string stem;
};

// Nur Wii/GC, bei denen weder covers_src-PNG noch .tex existiert.
std::vector<CoverJob> coverDownloadJobs(std::span<const Game* const> games,
                                        const std::function<bool(const std::string&)>& exists,
                                        std::string_view libRoot);

bool isPng(std::span<const u8> data);

struct DownloadSummary {
    u32 ok = 0, missing = 0, failed = 0;
};

}  // namespace nl
