#include "app/banner_service.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <ogc/cache.h>
#include <ogc/lwp_watchdog.h>

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

void BannerService::stopSound() {
    if (gate_.playing()) log::line("NL_BANNER sound stop");
    gate_.reset();
    if (audio_ != nullptr) audio_->freeBanner();
    if (music_ != nullptr) music_->duck(false);
}

void BannerService::updateSound() {
    if (audio_ == nullptr) return;
    const u64 ms = ticks_to_millisecs(gettime() - focusTicks_);
    const u32 focusMs = ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<u32>(ms);
    switch (gate_.update(audio_->bannerLoaded(), audio_->sounds(), focusMs)) {
        case BannerSoundGate::Action::Start:
            audio_->playBanner();
            if (music_ != nullptr) music_->duck(true);
            log::line("NL_BANNER sound play");
            break;
        case BannerSoundGate::Action::Stop:
            audio_->stopBanner();
            if (music_ != nullptr) music_->duck(false);
            log::line("NL_BANNER sound stop");
            break;
        case BannerSoundGate::Action::None: break;
    }
}

void BannerService::freeFrame() {
    stopSound();
    textures_.release();
    quads_.clear();
    player_.reset(nullptr, nullptr);
    start_.reset();
    loop_.reset();
    layout_ = Layout{};
    frame_ = ui::BannerFrame{};
    haveFrame_ = false;
}

void BannerService::takeChannel(ChannelBanner& banner) {
    if (!textures_.upload(banner, renderer_)) {
        log::line("NL_BANNER_FALLBACK cover memory");
        return;
    }
    layout_ = std::move(banner.layout);
    start_ = std::move(banner.start);
    loop_ = std::move(banner.loop);
    player_.reset(start_ ? &*start_ : nullptr, loop_ ? &*loop_ : nullptr);
    quads_ = buildQuads(layout_, initialState(layout_));
    u32 pictures = 0;
    for (const LytPane& p : layout_.panes) pictures += p.isPicture ? 1 : 0;
    frame_ = ui::BannerFrame{layout_.width, layout_.height, quads_};
    haveFrame_ = true;
    log::line("NL_BANNER panes=%u/%u skipped=%u tex=%u bytes=%u", static_cast<unsigned>(pictures),
              static_cast<unsigned>(layout_.panes.size()), static_cast<unsigned>(layout_.skipped),
              static_cast<unsigned>(layout_.textures.size()), static_cast<unsigned>(textures_.bytes()));
    log::line("NL_BANNER static quads=%u", static_cast<unsigned>(quads_.size()));
    if (banner.animInvalid) log::line("NL_BANNER_FALLBACK static brlan");
    if (banner.soundInvalid) log::line("NL_BANNER_FALLBACK mute bns");
    if (banner.sound && audio_ != nullptr) {
        const BnsSound& s = *banner.sound;
        if (audio_->loadBanner(s)) {
            log::line("NL_BANNER sound rate=%u samples=%u loop=%u bytes=%u", static_cast<unsigned>(s.sampleRate),
                      static_cast<unsigned>(s.pcm.size() / 2), static_cast<unsigned>(s.loop ? s.loopStart : 0),
                      static_cast<unsigned>(audio_->bannerBytes()));
        } else {
            log::line("NL_BANNER_FALLBACK mute memory");
        }
    }
    banner.sound.reset();  // nur eine PCM-Kopie im Speicher
    if (player_.current() != nullptr) {
        log::line("NL_BANNER anim start=%u loop=%u", static_cast<unsigned>(start_ ? start_->frames : 0),
                  static_cast<unsigned>(loop_ ? loop_->frames : 0));
    }  // das erste Bild setzt pump() direkt danach
}

void BannerService::animate() {
    const Anim* a = player_.current();
    if (a == nullptr) return;
    std::vector<PaneState> state = initialState(layout_);
    applyAnim(layout_, *a, player_.frame(), state);
    quads_ = buildQuads(layout_, state);
    frame_.quads = quads_;
    player_.step();
}

void BannerService::release() {
    freeTexels();
    freeFrame();
    key_.clear();
    requested_ = false;
}

void BannerService::want(const Game& game) {
    const char* key = game.key != nullptr ? game.key : "";
    if (key_ == key) return;
    // Anderes Spiel: Freigabe erst in pump(), der Renderer kann das alte Banner im laufenden Frame noch zeichnen.
    key_ = key;
    requested_ = false;
    focusTicks_ = gettime();
    path_.clear();
    if (game.device < roots_.size() && game.path != nullptr) path_ = joinPath(roots_[game.device], game.path);
}

ui::BannerView BannerService::view(const Game& game) const {
    if (game.key == nullptr || key_ != game.key) return {};
    if (haveFrame_) return ui::BannerView{0, layout_.width, layout_.height, &frame_};
    if (texels_ == nullptr) return {};
    return ui::BannerView{gfx::kBannerTextureBase, kGcBannerWidth, kGcBannerHeight};
}

void BannerService::pump() {
    if (auto r = worker_.take()) {
        if (r->key == key_) {
            freeTexels();
            freeFrame();
            if (r->channel) {
                takeChannel(*r->channel);
            } else if (r->gc) {
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
    if (haveFrame_) animate();  // 60 Bilder je Sekunde: pump() läuft einmal je Frame
    updateSound();
    if (key_.empty() || requested_ || worker_.running()) return;
    requested_ = true;
    freeTexels();  // altes Spiel: Renderer hat den Vorframe mit GX_DrawDone beendet
    freeFrame();
    if (!debugFile_.empty()) {
        if (!worker_.startChannel(fs_, key_, 0, debugFile_)) log::line("NL_BANNER_FALLBACK cover thread");
        return;
    }
    if (path_.empty()) {
        log::line("NL_BANNER_FALLBACK cover missing");
        return;
    }
    const bool shiftJis = key_.size() >= 4 && key_[3] == 'J';
    if (!worker_.startGc(fs_, key_, path_, german_ ? 1 : 0, shiftJis)) log::line("NL_BANNER_FALLBACK cover thread");
}

}  // namespace nl::app
