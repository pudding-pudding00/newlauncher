#include "app/banner_service.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <ogc/cache.h>

#include "platform/log.hpp"

namespace nl::app {

namespace {
const char* reasonOf(ErrorCode c) {
    switch (c) {
        case ErrorCode::NotFound: return "missing";
        case ErrorCode::InvalidData: return "invalid";
        default: return "io";
    }
}
}  // namespace

BannerService::~BannerService() { shutdown(); }

void BannerService::shutdown() {
    worker_.join();
    static_cast<void>(worker_.take());
}

void BannerService::freeTexels() {
    if (texels_ == nullptr) return;
    renderer_.setBannerTexture(0, nullptr, 0, 0, 0);
    std::free(texels_);
    texels_ = nullptr;
}

void BannerService::release() {
    freeTexels();
    key_.clear();
    requested_ = false;
}

void BannerService::want(const Game& game) {
    const char* key = game.key != nullptr ? game.key : "";
    if (key_ == key) return;
    // Anderes Spiel: Freigabe erst in pump(), der Renderer kann das alte Banner im laufenden Frame noch zeichnen.
    key_ = key;
    requested_ = false;
    path_.clear();
    if (game.device < roots_.size() && game.path != nullptr) path_ = joinPath(roots_[game.device], game.path);
}

ui::BannerView BannerService::view(const Game& game) const {
    if (texels_ == nullptr || game.key == nullptr || key_ != game.key) return {};
    return ui::BannerView{gfx::kBannerTextureBase, kGcBannerWidth, kGcBannerHeight};
}

void BannerService::pump() {
    if (auto r = worker_.take()) {
        if (r->key == key_) {
            freeTexels();
            if (r->gc) {
                void* p = memalign(32, r->gc->rgb5a3.size());
                if (p != nullptr) {
                    std::memcpy(p, r->gc->rgb5a3.data(), r->gc->rgb5a3.size());
                    DCFlushRange(p, r->gc->rgb5a3.size());  // GX liest am Cache vorbei
                    texels_ = p;
                    renderer_.setBannerTexture(0, p, kGcBannerWidth, kGcBannerHeight, gfx::kTexFormatRgb5a3);
                    log::line("NL_BANNER gc ok %s", r->key.c_str());
                } else {
                    log::line("NL_BANNER_FALLBACK cover memory");
                }
            } else {
                log::line("NL_BANNER_FALLBACK cover %s %s", reasonOf(r->error), r->context.c_str());
            }
        }
    }
    if (key_.empty() || requested_ || worker_.running()) return;
    requested_ = true;
    freeTexels();  // altes Spiel: Renderer hat den Vorframe mit GX_DrawDone beendet
    if (path_.empty()) {
        log::line("NL_BANNER_FALLBACK cover missing");
        return;
    }
    const bool shiftJis = key_.size() >= 4 && key_[3] == 'J';
    if (!worker_.startGc(fs_, key_, path_, german_ ? 1 : 0, shiftJis)) log::line("NL_BANNER_FALLBACK cover thread");
}

}  // namespace nl::app
