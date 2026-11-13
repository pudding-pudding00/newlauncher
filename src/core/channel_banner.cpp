#include "core/channel_banner.hpp"

#include <string>

#include "core/imd5.hpp"
#include "core/u8_archive.hpp"
#include "core/wii_title.hpp"

namespace nl {

namespace {
Result<ChannelBanner> fail(ErrorCode c, std::string what) { return Result<ChannelBanner>::err(Error{c, std::move(what)}); }
}  // namespace

Result<ChannelBanner> loadChannelBanner(std::span<const u8> content0) {
    const auto at = imetArchiveOffset(content0);
    if (!at || *at >= content0.size()) return fail(ErrorCode::InvalidData, "Banner: kein IMET");
    auto outer = U8Archive::parse(content0.subspan(*at));
    if (!outer.isOk()) return fail(ErrorCode::InvalidData, "Banner: U8 außen");
    const auto bin = outer.value().file("meta/banner.bin");
    if (!bin) return fail(ErrorCode::NotFound, "Banner: banner.bin fehlt");
    auto unpacked = unwrapImd5(*bin, kBannerBinMax);
    if (!unpacked.isOk()) return fail(ErrorCode::InvalidData, "Banner: banner.bin");

    ChannelBanner b;
    auto archive = std::make_shared<const std::vector<u8>>(std::move(unpacked.value()));
    auto inner = U8Archive::parse(*archive);
    if (!inner.isOk()) return fail(ErrorCode::InvalidData, "Banner: U8 innen");
    const auto lyt = inner.value().file("arc/blyt/banner.brlyt");
    if (!lyt) return fail(ErrorCode::NotFound, "Banner: banner.brlyt fehlt");
    auto layout = parseBrlyt(*lyt);
    if (!layout.isOk()) return fail(ErrorCode::InvalidData, "Banner: brlyt");
    b.layout = std::move(layout.value());
    b.textures.reserve(b.layout.textures.size());
    for (const std::string& name : b.layout.textures) {
        const auto tpl = inner.value().file("arc/timg/" + name);
        if (!tpl) return fail(ErrorCode::InvalidData, "Banner: Textur fehlt " + name);
        auto images = parseTpl(*tpl);
        if (!images.isOk()) return fail(ErrorCode::InvalidData, "Banner: TPL " + name);
        b.textures.push_back(images.value().front());
    }
    b.archive = std::move(archive);
    return Result<ChannelBanner>::ok(std::move(b));
}

}  // namespace nl
