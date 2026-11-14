#include "core/banner_budget.hpp"

#include <algorithm>
#include <memory>

#include "core/banner_scene.hpp"

namespace nl {

namespace {
bool paletted(TexFmt f) { return f == TexFmt::C4 || f == TexFmt::C8 || f == TexFmt::C14X2; }

u32 roundUp32(u64 v) {
    const u64 r = (v + 31) & ~u64{31};
    return r > 0xFFFFFFFFu ? 0xFFFFFFE0u : static_cast<u32>(r);
}

u32 add(u32 a, u64 b) {
    const u64 s = static_cast<u64>(a) + b;
    return s > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<u32>(s);
}

BannerCost costOf(const ChannelBanner& b, std::size_t textures, u32 pcmSamples) {
    const std::size_t n = std::min(textures, b.textures.size());
    return estimateCost(b.layout, std::span<const TplImage>(b.textures.data(), n), pcmSamples);
}

u32 samplesOf(const ChannelBanner& b) {
    if (!b.sound) return 0;
    const std::size_t s = b.sound->pcm.size() / 2;  // verschränkt L/R
    return s > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<u32>(s);
}
}  // namespace

u32 uploadBytes(const TplImage& img) {
    return texBytes(paletted(img.fmt) ? TexFmt::RGB5A3 : img.fmt, img.w, img.h);
}

BannerCost estimateCost(const Layout& l, std::span<const TplImage> tex, u32 pcmSamples) {
    BannerCost c;
    for (const TplImage& t : tex) c.textures = add(c.textures, uploadBytes(t));
    if (pcmSamples > 0) c.pcm = roundUp32(static_cast<u64>(pcmSamples) * 4);  // Stereo, 16 Bit
    const u64 perPane = sizeof(LytPane) + sizeof(Quad) + sizeof(PaneState);
    c.parsed = add(c.parsed, perPane * l.panes.size() + sizeof(LytMaterial) * l.materials.size());
    return c;
}

bool withinBudget(const BannerCost& c) { return c.total() <= kBannerBudget; }

BannerLevel chooseLevel(bool layoutOk, bool animOk, bool budgetOk) {
    if (!layoutOk) return BannerLevel::Cover;
    return animOk && budgetOk ? BannerLevel::Animated : BannerLevel::Static;
}

const char* bannerLevelName(BannerLevel l) {
    switch (l) {
        case BannerLevel::Animated: return "anim";
        case BannerLevel::Static: return "static";
        case BannerLevel::Cover: return "cover";
    }
    return "cover";
}

BannerPlan planBanner(const ChannelBanner& b) {
    // Voll: alle Texturen (bei kaputter Animation hat der Lader die Muster schon entfernt) und der Ton.
    const BannerCost full = costOf(b, b.textures.size(), samplesOf(b));
    const BannerCost still = costOf(b, b.baseTextures, 0);
    const bool budgetOk = withinBudget(full);
    // Der Renderer hat kBannerMaxTextures Slots: zu viele Muster → Static, zu viele Basistexturen → Cover.
    const bool slotsOk = b.textures.size() <= kBannerMaxTextures;
    const bool baseSlotsOk = std::min(b.baseTextures, b.textures.size()) <= kBannerMaxTextures;
    BannerPlan p;
    p.level = chooseLevel(withinBudget(still) && baseSlotsOk, !b.animInvalid, budgetOk && slotsOk);
    p.budgetCut = !budgetOk;
    p.textureCut = !slotsOk;
    p.cost = budgetOk && slotsOk ? full : still;
    return p;
}

BannerPlan prepareBanner(ChannelBanner& b) {
    BannerPlan p = planBanner(b);
    if (p.level == BannerLevel::Cover) return p;
    if (p.budgetCut || p.textureCut) reduceToStatic(b);
    for (TplImage& t : b.textures) {
        if (!paletted(t.fmt)) continue;
        auto tiles = std::make_shared<std::vector<u8>>(toRgb5a3Tiles(t));
        if (tiles->empty()) {
            p.level = BannerLevel::Cover;
            p.convertFailed = true;
            return p;
        }
        t.data = std::span<const u8>(*tiles);
        t.fmt = TexFmt::RGB5A3;
        t.palette = {};
        t.palCount = 0;
        b.converted.push_back(std::move(tiles));
    }
    return p;
}

void reduceToStatic(ChannelBanner& b) {
    const std::size_t n = std::min(b.baseTextures, b.textures.size());
    b.textures.resize(n);
    if (b.layout.textures.size() > n) b.layout.textures.resize(n);
    b.start.reset();
    b.loop.reset();
    b.sound.reset();
}

}  // namespace nl
