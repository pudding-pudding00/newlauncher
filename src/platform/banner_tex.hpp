#pragma once
// Texturen eines Kanal-Banners in die Renderer-Slots (M18). TPL-Daten sind schon GX-Kacheln und werden nur in
// 32-Byte-ausgerichteten Speicher kopiert; Palettenformate werden vorher zu RGB5A3 (keine TLUT-Slots nötig).
// Nur Haupt-Thread. release() erst, wenn der Renderer den Vorframe beendet hat (GX_DrawDone in endFrame).

#include <vector>

#include "core/channel_banner.hpp"
#include "gfx/renderer.hpp"

namespace nl::platform {

class BannerTextures {
public:
    BannerTextures() = default;
    BannerTextures(const BannerTextures&) = delete;
    BannerTextures& operator=(const BannerTextures&) = delete;
    ~BannerTextures();

    // false: mehr Texturen als Slots oder kein Speicher; dann ist nichts belegt.
    bool upload(const ChannelBanner& banner, gfx::Renderer& renderer);
    void release();
    u32 bytes() const { return bytes_; }

private:
    gfx::Renderer* renderer_ = nullptr;
    std::vector<void*> texels_;
    u32 bytes_ = 0;
};

}  // namespace nl::platform
