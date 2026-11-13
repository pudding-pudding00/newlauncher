#include "core/channel_banner.hpp"
#include "support/bns_image.hpp"
#include "support/brlan_images.hpp"
#include "support/brlyt_images.hpp"
#include "support/tpl_images.hpp"
#include "support/u8_images.hpp"

#include <cstring>

#include <doctest/doctest.h>

using namespace nl;
using test::FstSpec;

namespace {
FstSpec file(std::string name, std::vector<u8> data) { return FstSpec{std::move(name), std::move(data), false, {}}; }
FstSpec dir(std::string name, std::vector<FstSpec> children) { return FstSpec{std::move(name), {}, true, std::move(children)}; }

std::vector<u8> tplOf(u8 red) {
    return test::tplFile({test::TplSpec{TexFmt::RGB5A3, 8, 4, [red](int, int) { return test::Rgba{red, 0, 0, 255}; }, {}}});
}

std::vector<u8> innerArchive(bool withTexture, std::vector<FstSpec> anim = {}, std::vector<FstSpec> moreTex = {}) {
    test::PaneSpec pic = test::pane("pic1", "p", 0, 0, 8, 8);
    const auto brlyt = test::buildBrlyt(608, 456, {"logo.tpl"}, {test::MatSpec{"m", {0}, {}, {}}}, {pic});
    const auto tpl = test::tplFile({test::TplSpec{TexFmt::RGB5A3, 8, 4, [](int, int) { return test::Rgba{255, 0, 0, 255}; }, {}}});
    std::vector<FstSpec> timg;
    if (withTexture) timg.push_back(file("logo.tpl", tpl));
    for (auto& t : moreTex) timg.push_back(std::move(t));
    std::vector<FstSpec> arc{dir("blyt", {file("banner.brlyt", brlyt)}), dir("timg", timg)};
    if (!anim.empty()) arc.insert(arc.begin(), dir("anim", std::move(anim)));
    return test::buildU8({dir("arc", std::move(arc))});
}

std::vector<u8> content0(const std::vector<u8>& bannerBin, std::optional<std::vector<u8>> soundBin = std::nullopt) {
    std::vector<u8> c(0x600, 0);
    std::memcpy(c.data() + 0x40, "IMET", 4);
    std::vector<FstSpec> meta{file("banner.bin", bannerBin)};
    if (soundBin) meta.push_back(file("sound.bin", *soundBin));
    const auto outer = test::buildU8({dir("meta", std::move(meta))});
    c.insert(c.end(), outer.begin(), outer.end());
    return c;
}
}  // namespace

TEST_CASE("channel_banner: IMET → U8 → banner.bin → Layout und Texturen") {
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(true))));
    REQUIRE(r.isOk());
    const ChannelBanner b = r.value();  // Kopie: Texturen bleiben über das geteilte Archiv gültig
    r = Result<ChannelBanner>::err(Error{ErrorCode::NotFound, ""});
    CHECK(b.layout.width == 608.0f);
    REQUIRE(b.layout.panes.size() == 1);
    REQUIRE(b.textures.size() == 1);
    CHECK(b.textures[0].fmt == TexFmt::RGB5A3);
    CHECK(b.textures[0].w == 8);
    CHECK(b.textures[0].h == 4);
    const auto rgba = decodeToRgba(b.textures[0]);
    REQUIRE(rgba.size() == 8 * 4 * 4);
    CHECK(rgba[0] == 255);
    CHECK(rgba[1] == 0);
}

TEST_CASE("channel_banner: Fehler fallen sauber zurück") {
    CHECK(loadChannelBanner(std::vector<u8>(0x700, 0)).error().code == ErrorCode::InvalidData);  // kein IMET
    std::vector<u8> noBin(0x600, 0);
    std::memcpy(noBin.data() + 0x40, "IMET", 4);
    const auto outer = test::buildU8({dir("meta", {file("icon.bin", {1, 2, 3})})});
    noBin.insert(noBin.end(), outer.begin(), outer.end());
    CHECK(loadChannelBanner(noBin).error().code == ErrorCode::NotFound);
    CHECK(loadChannelBanner(content0(test::imd5(innerArchive(false)))).error().code == ErrorCode::InvalidData);
    CHECK(loadChannelBanner(content0({1, 2, 3})).error().code == ErrorCode::InvalidData);  // kein IMD5
}

namespace {
std::vector<u8> moveAnim(u16 frames, std::vector<std::string> patterns = {}) {
    std::vector<test::TagSpec> tags{test::TagSpec{"RLPA", {test::InfoSpec{0, 0, false, {{0, 0, 0}, {10, 50, 0}}}}}};
    if (!patterns.empty()) tags.push_back(test::TagSpec{"RLTP", {test::InfoSpec{0, 0, true, {{0, 0, 0}}}}});
    std::vector<test::EntrySpec> entries{test::EntrySpec{"p", false, {tags[0]}}};
    if (!patterns.empty()) entries.push_back(test::EntrySpec{"m", true, {tags[1]}});
    return test::buildBrlan(frames, true, patterns, entries);
}
}  // namespace

TEST_CASE("channel_banner: Start- und Loop-Animation, Mustertexturen werden mitgeladen") {
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(
        true, {file("banner_Start.brlan", moveAnim(30)), file("banner_Loop.brlan", moveAnim(120, {"pat.tpl", "logo.tpl"}))},
        {file("pat.tpl", tplOf(9))}))));
    REQUIRE(r.isOk());
    const ChannelBanner& b = r.value();
    REQUIRE(b.start);
    REQUIRE(b.loop);
    CHECK(b.start->frames == 30);
    CHECK(b.loop->frames == 120);
    CHECK_FALSE(b.animInvalid);
    CHECK(b.layout.textures == std::vector<std::string>{"logo.tpl", "pat.tpl"});  // pat.tpl angehängt, logo nicht doppelt
    REQUIRE(b.textures.size() == 2);
    CHECK(decodeToRgba(b.textures[1])[0] == 8);  // 9 auf 5 Bit gerundet
}

TEST_CASE("channel_banner: nur banner.brlan ist die Schleife") {
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(true, {file("banner.brlan", moveAnim(60))}))));
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().start);
    REQUIRE(r.value().loop);
    CHECK(r.value().loop->frames == 60);
}

TEST_CASE("channel_banner: kaputte Animation oder fehlende Mustertextur → stehendes Bild") {
    auto broken = moveAnim(60);
    broken[0] = 'X';
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(true, {file("banner_Loop.brlan", broken)}))));
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().loop);
    CHECK(r.value().animInvalid);
    r = loadChannelBanner(content0(test::imd5(innerArchive(true, {file("banner_Loop.brlan", moveAnim(60, {"fehlt.tpl"}))}))));
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().loop);
    CHECK(r.value().animInvalid);
    CHECK(r.value().layout.textures.size() == 1);
    CHECK(r.value().textures.size() == 1);
}

TEST_CASE("channel_banner: sound.bin wird über IMD5 und BNS geladen") {
    const std::vector<s16> pcm(56, 1000);
    const auto bns = test::buildBns({pcm}, 32000, true, 14);
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(true)), test::imd5(bns.file)));
    REQUIRE(r.isOk());
    REQUIRE(r.value().sound.has_value());
    CHECK_FALSE(r.value().soundInvalid);
    CHECK(r.value().sound->sampleRate == 32000);
    CHECK(r.value().sound->pcm.size() == 112);
    CHECK(r.value().sound->loopStart == 14);
}

TEST_CASE("channel_banner: ohne oder mit kaputtem sound.bin bleibt das Banner stumm") {
    auto r = loadChannelBanner(content0(test::imd5(innerArchive(true))));
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().sound.has_value());
    CHECK_FALSE(r.value().soundInvalid);
    r = loadChannelBanner(content0(test::imd5(innerArchive(true)), test::imd5({'B', 'N', 'S', ' ', 0, 0})));
    REQUIRE(r.isOk());
    CHECK_FALSE(r.value().sound.has_value());
    CHECK(r.value().soundInvalid);
    r = loadChannelBanner(content0(test::imd5(innerArchive(true)), std::vector<u8>{1, 2, 3}));  // kein IMD5
    REQUIRE(r.isOk());
    CHECK(r.value().soundInvalid);
}
