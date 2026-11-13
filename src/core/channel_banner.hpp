#pragma once
// Kanal-Banner auspacken (M18): Content 0 → IMET-Kopf → U8 → meta/banner.bin (IMD5, ggf. LZ77) → inneres
// U8 → arc/blyt/banner.brlyt und arc/timg/<name> für jeden txl1-Eintrag. Fehlt eine Textur, gilt das
// Banner als kaputt (Rückfall aufs Cover), damit nie ein Bild mit Lücken erscheint.

#include <memory>
#include <span>
#include <vector>

#include "core/brlyt.hpp"
#include "core/result.hpp"
#include "core/tpl.hpp"

namespace nl {

inline constexpr u32 kBannerBinMax = 4u * 1024 * 1024;  // entpacktes banner.bin, Budget aus der Spec

struct ChannelBanner {
    // Entpacktes banner.bin; die Texturen zeigen hinein. shared_ptr, damit Kopien gültig bleiben.
    std::shared_ptr<const std::vector<u8>> archive;
    Layout layout;
    std::vector<TplImage> textures;  // je txl1-Eintrag das erste Bild der TPL
};

Result<ChannelBanner> loadChannelBanner(std::span<const u8> content0);

}  // namespace nl
