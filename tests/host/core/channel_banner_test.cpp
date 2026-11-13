#include "core/channel_banner.hpp"
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

std::vector<u8> innerArchive(bool withTexture) {
    test::PaneSpec pic = test::pane("pic1", "p", 0, 0, 8, 8);
    const auto brlyt = test::buildBrlyt(608, 456, {"logo.tpl"}, {test::MatSpec{"m", {0}, {}, {}}}, {pic});
    const auto tpl = test::tplFile({test::TplSpec{TexFmt::RGB5A3, 8, 4, [](int, int) { return test::Rgba{255, 0, 0, 255}; }, {}}});
    std::vector<FstSpec> timg;
    if (withTexture) timg.push_back(file("logo.tpl", tpl));
    return test::buildU8({dir("arc", {dir("blyt", {file("banner.brlyt", brlyt)}), dir("timg", timg)})});
}

std::vector<u8> content0(const std::vector<u8>& bannerBin) {
    std::vector<u8> c(0x600, 0);
    std::memcpy(c.data() + 0x40, "IMET", 4);
    const auto outer = test::buildU8({dir("meta", {file("banner.bin", bannerBin)})});
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
