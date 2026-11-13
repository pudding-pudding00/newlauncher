#pragma once
// Banner-Ton (M18): spielt erst nach 1 s Fokus, nur mit eingeschalteten Klängen und geladenem Ton; endet,
// sobald eine Bedingung wegfällt. Reine Entscheidung, das Abspielen macht platform::Audio.

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kBannerSoundDelayMs = 1000;

class BannerSoundGate {
public:
    enum class Action : u8 { None, Start, Stop };
    Action update(bool loaded, bool soundsOn, u32 focusMs);
    void reset() { playing_ = false; }
    bool playing() const { return playing_; }

private:
    bool playing_ = false;
};

}  // namespace nl
