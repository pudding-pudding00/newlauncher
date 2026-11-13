#include "core/banner_sound.hpp"

namespace nl {

BannerSoundGate::Action BannerSoundGate::update(bool loaded, bool soundsOn, u32 focusMs, bool voiceEnded) {
    if (playing_ && voiceEnded) ended_ = true;
    const bool want = !ended_ && loaded && soundsOn && focusMs >= kBannerSoundDelayMs;
    if (want == playing_) return Action::None;
    playing_ = want;
    return want ? Action::Start : Action::Stop;
}

}  // namespace nl
