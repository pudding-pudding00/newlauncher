#include "core/banner_budget.hpp"

#include <doctest/doctest.h>

using namespace nl;

namespace {
TplImage img(TexFmt f, u16 w, u16 h) {
    TplImage t;
    t.fmt = f;
    t.w = w;
    t.h = h;
    return t;
}
ChannelBanner bannerWith(std::vector<TplImage> tex, std::size_t base, bool anim, u32 samples) {
    ChannelBanner b;
    b.layout.width = 608;
    b.layout.height = 456;
    b.layout.panes.resize(3);
    for (std::size_t i = 0; i < tex.size(); ++i) b.layout.textures.push_back("t" + std::to_string(i));
    b.textures = std::move(tex);
    b.baseTextures = base;
    if (anim) b.loop = Anim{};
    if (samples > 0) {
        b.sound = BnsSound{};
        b.sound->pcm.resize(static_cast<std::size_t>(samples) * 2);
    }
    return b;
}
constexpr u32 kMiB = 1024 * 1024;
}  // namespace

TEST_CASE("banner_budget: Kosten = Texel (Palette als RGB5A3) + PCM + Strukturen") {
    CHECK(uploadBytes(img(TexFmt::RGBA8, 64, 32)) == texBytes(TexFmt::RGBA8, 64, 32));
    CHECK(uploadBytes(img(TexFmt::C4, 64, 32)) == texBytes(TexFmt::RGB5A3, 64, 32));
    CHECK(uploadBytes(img(TexFmt::C8, 32, 16)) == texBytes(TexFmt::RGB5A3, 32, 16));
    CHECK(uploadBytes(img(TexFmt::CMPR, 64, 64)) == texBytes(TexFmt::CMPR, 64, 64));
    const std::vector<TplImage> tex{img(TexFmt::RGB5A3, 64, 48), img(TexFmt::C8, 32, 16)};
    Layout l;
    l.panes.resize(4);
    l.materials.resize(2);
    const BannerCost c = estimateCost(l, tex, 12000);
    CHECK(c.textures == texBytes(TexFmt::RGB5A3, 64, 48) + texBytes(TexFmt::RGB5A3, 32, 16));
    CHECK(c.pcm == 48000);
    CHECK(estimateCost(l, tex, 3).pcm == 32);  // 12 Byte → 32
    CHECK(c.parsed > 0);
    CHECK(c.total() == static_cast<u64>(c.textures) + c.pcm + c.parsed);
    CHECK(withinBudget(c));
}

TEST_CASE("banner_budget: 5 MiB sprengen das Budget") {
    BannerCost c;
    c.textures = 5 * kMiB;
    CHECK_FALSE(withinBudget(c));
    c.textures = kBannerBudget;
    CHECK(withinBudget(c));
    c.parsed = 1;
    CHECK_FALSE(withinBudget(c));
}

TEST_CASE("banner_budget: chooseLevel") {
    CHECK(chooseLevel(true, true, true) == BannerLevel::Animated);
    CHECK(chooseLevel(true, false, true) == BannerLevel::Static);   // Animation kaputt
    CHECK(chooseLevel(true, true, false) == BannerLevel::Static);   // erstes Bild passt allein
    CHECK(chooseLevel(false, true, false) == BannerLevel::Cover);   // nicht einmal das
    CHECK(chooseLevel(false, true, true) == BannerLevel::Cover);
    CHECK(std::string(bannerLevelName(BannerLevel::Static)) == "static");
}

TEST_CASE("banner_budget: planBanner und reduceToStatic") {
    // Basis 2 MiB (RGBA8 1024×512), Mustertextur 2 MiB, Ton 1 MiB → 5 MiB: Static.
    auto b = bannerWith({img(TexFmt::RGBA8, 1024, 512), img(TexFmt::RGBA8, 1024, 512)}, 1, true, 256 * 1024);
    BannerPlan p = planBanner(b);
    CHECK(p.level == BannerLevel::Static);
    CHECK(p.budgetCut);
    CHECK(p.cost.textures == 2 * kMiB);
    CHECK(p.cost.pcm == 0);
    reduceToStatic(b);
    CHECK_FALSE(b.loop.has_value());
    CHECK_FALSE(b.sound.has_value());
    CHECK(b.textures.size() == 1);
    CHECK(b.layout.textures.size() == 1);

    // Ohne Ton passt alles: Animated.
    b = bannerWith({img(TexFmt::RGBA8, 1024, 512), img(TexFmt::RGBA8, 512, 512)}, 1, true, 0);
    p = planBanner(b);
    CHECK(p.level == BannerLevel::Animated);
    CHECK_FALSE(p.budgetCut);
    CHECK(p.cost.textures == 3 * kMiB);

    // Schon die Basis sprengt das Budget (zwei RGBA8 1024×1024 = 8 MiB): Cover.
    b = bannerWith({img(TexFmt::RGBA8, 1024, 1024), img(TexFmt::RGBA8, 1024, 1024)}, 2, false, 0);
    p = planBanner(b);
    CHECK(p.level == BannerLevel::Cover);
    CHECK(p.budgetCut);

    // Kaputte Animation, Budget egal: Static ohne Budgetschnitt.
    b = bannerWith({img(TexFmt::RGB5A3, 64, 64)}, 1, false, 100);
    b.animInvalid = true;
    p = planBanner(b);
    CHECK(p.level == BannerLevel::Static);
    CHECK_FALSE(p.budgetCut);
}
