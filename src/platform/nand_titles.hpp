#pragma once
// Installierte Kanäle im NAND (Spec 6.7): Title-IDs über ES, Namen aus dem IMET-Banner über ISFS.
// Fehler werden nicht gemeldet, sondern ergeben leere Listen bzw. leere Namen.

#include <optional>
#include <span>
#include <vector>

#include "core/types.hpp"
#include "core/vc_map.hpp"
#include "core/wii_title.hpp"
#include "ui/strings.hpp"

namespace nl::platform {

std::vector<u64> installedTitleIds();
std::optional<u32> bannerContentId(u64 titleId);  // Content-ID mit Index 0 aus dem gespeicherten TMD  // nur Kanäle (isChannelTitle)

// Sortiert nach channelDisplayName, Groß/klein egal. Ruft beim ersten Aufruf ISFS_Initialize.
std::vector<ChannelInfo> loadChannelNames(std::span<const u64> ids, BannerLang lang);

BannerLang systemBannerLang();  // CONF_GetLanguage; unbekannt → English
// Kanalnamen in der Sprache des Launchers: De → German, En → English.
constexpr BannerLang bannerLangFor(ui::Lang l) { return l == ui::Lang::De ? BannerLang::German : BannerLang::English; }
const char* systemCoverRegion();  // GameTDB-Region aus CONF_GetLanguage: DE, FR, JA, sonst EN

}  // namespace nl::platform
