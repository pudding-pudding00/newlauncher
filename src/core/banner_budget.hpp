#pragma once
// Speicherbudget eines Kanal-Banners (M18, Spec: ein Banner zugleich, ≤ 4 MiB MEM2 inkl. Texturen und
// PCM-Puffer). Die Größen kommen aus den Köpfen, bevor etwas hochgeladen oder kopiert wird.
// Rückfallkette: Animated (alles) → Static (erstes Bild, ohne Animation und Ton) → Cover.

#include <span>

#include "core/brlyt.hpp"
#include "core/channel_banner.hpp"
#include "core/tpl.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr u32 kBannerBudget = 4u * 1024 * 1024;
inline constexpr std::size_t kBannerMaxTextures = 64;  // Textur-Slots des Renderers (gfx::kBannerTextureSlots)

struct BannerCost {
    u32 textures = 0;  // hochgeladene Texel (Palettenformate als RGB5A3)
    u32 pcm = 0;       // Stereo-PCM, auf 32 Byte gerundet
    u32 parsed = 0;    // Layout-Strukturen und Quads
    u64 total() const { return static_cast<u64>(textures) + pcm + parsed; }
};

u32 uploadBytes(const TplImage& img);  // Palettenformate → texBytes(RGB5A3), sonst texBytes
BannerCost estimateCost(const Layout& l, std::span<const TplImage> tex, u32 pcmSamples);
bool withinBudget(const BannerCost& c);  // Summe ≤ kBannerBudget

enum class BannerLevel : u8 { Animated, Static, Cover };
// layoutOk: Layout lesbar und das erste Bild passt allein ins Budget; budgetOk: alles passt.
BannerLevel chooseLevel(bool layoutOk, bool animOk, bool budgetOk);
const char* bannerLevelName(BannerLevel l);  // anim, static, cover

struct BannerPlan {
    BannerLevel level = BannerLevel::Cover;
    BannerCost cost;     // des gewählten Levels
    bool budgetCut = false;  // Level wegen des Budgets gesenkt
    bool textureCut = false;     // Level wegen zu vieler Texturen (> kBannerMaxTextures) gesenkt
    bool convertFailed = false;  // Palettenbild ließ sich nicht umwandeln → Cover
};
BannerPlan planBanner(const ChannelBanner& b);
// Static: Animationen, Mustertexturen und Ton weg; die Basistexturen bleiben.
void reduceToStatic(ChannelBanner& b);
// Im Banner-Thread: planen, bei Bedarf auf Static kürzen und Palettenbilder (C4/C8/C14X2) in RGB5A3-Kacheln
// umwandeln, damit der Haupt-Thread nur noch kopiert. Bei Cover bleibt das Banner unverändert.
BannerPlan prepareBanner(ChannelBanner& b);

}  // namespace nl
