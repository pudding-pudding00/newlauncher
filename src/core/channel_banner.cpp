#include "core/channel_banner.hpp"

#include <algorithm>
#include <string>

#include "core/imd5.hpp"
#include "core/u8_archive.hpp"
#include "core/wii_title.hpp"

namespace nl {

namespace {
Result<ChannelBanner> fail(ErrorCode c, std::string what) { return Result<ChannelBanner>::err(Error{c, std::move(what)}); }

std::optional<TplImage> firstImage(const U8Archive& arc, const std::string& name) {
    const auto tpl = arc.file("arc/timg/" + name);
    if (!tpl) return std::nullopt;
    auto images = parseTpl(*tpl);
    if (!images.isOk()) return std::nullopt;
    return images.value().front();
}

// Animation laden; nullopt ohne Datei. Kaputt → invalid gesetzt.
std::optional<Anim> loadAnim(const U8Archive& arc, const char* path, bool& invalid) {
    const auto file = arc.file(path);
    if (!file) return std::nullopt;
    auto a = parseBrlan(*file);
    if (!a.isOk()) {
        invalid = true;
        return std::nullopt;
    }
    return std::move(a.value());
}

// Mustertexturen der Animation in Layout und Texturliste aufnehmen; false, wenn eine fehlt.
bool addPatterns(const U8Archive& arc, const Anim& a, ChannelBanner& b) {
    for (const std::string& name : a.patternTextures) {
        if (std::find(b.layout.textures.begin(), b.layout.textures.end(), name) != b.layout.textures.end()) continue;
        if (b.layout.textures.size() >= kLytMaxTextures) return false;
        auto img = firstImage(arc, name);
        if (!img) return false;
        b.layout.textures.push_back(name);
        b.textures.push_back(*img);
    }
    return true;
}
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
        auto img = firstImage(inner.value(), name);
        if (!img) return fail(ErrorCode::InvalidData, "Banner: Textur " + name);
        b.textures.push_back(*img);
    }
    b.start = loadAnim(inner.value(), "arc/anim/banner_Start.brlan", b.animInvalid);
    b.loop = loadAnim(inner.value(), "arc/anim/banner_Loop.brlan", b.animInvalid);
    if (!b.loop && !b.animInvalid) b.loop = loadAnim(inner.value(), "arc/anim/banner.brlan", b.animInvalid);
    const std::size_t baseTextures = b.textures.size();
    if (b.animInvalid || (b.start && !addPatterns(inner.value(), *b.start, b)) || (b.loop && !addPatterns(inner.value(), *b.loop, b))) {
        b.animInvalid = true;
        b.start.reset();
        b.loop.reset();
        b.textures.resize(baseTextures);
        b.layout.textures.resize(baseTextures);
    }
    if (const auto snd = outer.value().file("meta/sound.bin")) {
        auto raw = unwrapImd5(*snd, kSoundBinMax);
        auto parsed = raw.isOk() ? parseBns(raw.value(), kBnsMaxSamples) : Result<BnsSound>::err(raw.error());
        if (parsed.isOk())
            b.sound = std::move(parsed.value());
        else
            b.soundInvalid = true;
    }
    b.archive = std::move(archive);
    return Result<ChannelBanner>::ok(std::move(b));
}

}  // namespace nl
