#include "platform/banner_tex.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <ogc/cache.h>

namespace nl::platform {

namespace {
bool isPaletted(TexFmt f) { return f == TexFmt::C4 || f == TexFmt::C8 || f == TexFmt::C14X2; }
}  // namespace

BannerTextures::~BannerTextures() { release(); }

bool BannerTextures::upload(const ChannelBanner& banner, gfx::Renderer& renderer) {
    release();
    if (banner.textures.size() > gfx::kBannerTextureSlots) return false;
    renderer_ = &renderer;
    for (std::size_t i = 0; i < banner.textures.size(); ++i) {
        const TplImage& img = banner.textures[i];
        const std::span<const u8> data = img.data;
        const TexFmt fmt = img.fmt;
        if (isPaletted(fmt)) {  // prepareBanner hat umgewandelt; ohne TLUT nicht darstellbar
            release();
            return false;
        }
        void* p = memalign(32, data.size());
        if (p == nullptr) {
            release();
            return false;
        }
        std::memcpy(p, data.data(), data.size());
        DCFlushRange(p, data.size());  // GX liest am Cache vorbei
        texels_.push_back(p);
        bytes_ += static_cast<u32>(data.size());
        renderer.setBannerTexture(static_cast<u16>(i), p, img.w, img.h, static_cast<u8>(fmt));
    }
    return true;
}

void BannerTextures::release() {
    for (std::size_t i = 0; i < texels_.size(); ++i) {
        if (renderer_ != nullptr) renderer_->setBannerTexture(static_cast<u16>(i), nullptr, 0, 0, 0);
        std::free(texels_[i]);
    }
    texels_.clear();
    bytes_ = 0;
}

}  // namespace nl::platform
