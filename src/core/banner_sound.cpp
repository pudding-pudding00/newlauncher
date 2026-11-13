#include "core/banner_sound.hpp"

namespace nl {

BannerSoundGate::Action BannerSoundGate::update(bool loaded, bool soundsOn, u32 focusMs) {
    const bool want = loaded && soundsOn && focusMs >= kBannerSoundDelayMs;
    if (want == playing_) return Action::None;
    playing_ = want;
    return want ? Action::Start : Action::Stop;
}

}  // namespace nl
