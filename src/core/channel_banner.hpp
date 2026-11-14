#pragma once
// Kanal-Banner auspacken (M18): Content 0 → IMET-Kopf → U8 → meta/banner.bin (IMD5, ggf. LZ77) → inneres
// U8 → arc/blyt/banner.brlyt und arc/timg/<name> für jeden txl1-Eintrag. Fehlt eine Textur, gilt das
// Banner als kaputt (Rückfall aufs Cover), damit nie ein Bild mit Lücken erscheint.
// Animationen (Task 6): arc/anim/banner_Start.brlan einmal, banner_Loop.brlan in Schleife; nur banner.brlan
// → Schleife. Ist eine Animation kaputt oder fehlt eine ihrer Mustertexturen, bleibt das Bild stehen
// (animInvalid) statt aufs Cover zu fallen. Mustertexturen werden an layout.textures angehängt.
// Ton (Task 7): meta/sound.bin (IMD5 → BNS) neben banner.bin; kaputt → stumm (soundInvalid).

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/bns.hpp"
#include "core/brlan.hpp"
#include "core/brlyt.hpp"
#include "core/result.hpp"
#include "core/tpl.hpp"

namespace nl {

inline constexpr u32 kBannerBinMax = 4u * 1024 * 1024;  // entpacktes banner.bin, Budget aus der Spec
inline constexpr u32 kSoundBinMax = 1u * 1024 * 1024;  // entpacktes sound.bin (ADPCM, ~3,6 Bit je Sample)

struct ChannelBanner {
    // Entpacktes banner.bin; die Texturen zeigen hinein. shared_ptr, damit Kopien gültig bleiben.
    std::shared_ptr<const std::vector<u8>> archive;
    Layout layout;
    std::vector<TplImage> textures;  // je Eintrag in layout.textures das erste Bild der TPL
    std::size_t baseTextures = 0;    // Texturen aus txl1; dahinter die Mustertexturen der Animation
    std::optional<Anim> start, loop;
    bool animInvalid = false;
    std::optional<BnsSound> sound;  // meta/sound.bin; fehlt er oder ist kaputt, bleibt das Banner stumm
    bool soundInvalid = false;
    // Umgewandelte Palettenbilder (prepareBanner); textures[i].data zeigt dann hierhin.
    std::vector<std::shared_ptr<const std::vector<u8>>> converted;
};

Result<ChannelBanner> loadChannelBanner(std::span<const u8> content0);

}  // namespace nl
