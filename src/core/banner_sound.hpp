#pragma once
// Banner-Ton (M18): spielt erst nach 1 s Fokus, nur mit eingeschalteten Klängen und geladenem Ton; endet,
// sobald eine Bedingung wegfällt. Reine Entscheidung, das Abspielen macht platform::Audio.

#include "core/types.hpp"

namespace nl {

inline constexpr u32 kBannerSoundDelayMs = 1000;

class BannerSoundGate {
public:
    enum class Action : u8 { None, Start, Stop };
    // voiceEnded: die Stimme ist nach Start von selbst frei geworden (Ton ohne Schleife zu Ende).
    Action update(bool loaded, bool soundsOn, u32 focusMs, bool voiceEnded = false);
    void reset() {
        playing_ = false;
        ended_ = false;
    }
    bool playing() const { return playing_; }

private:
    bool playing_ = false;
    bool ended_ = false;  // Ton lief bis zum Ende (ohne Schleife); erst reset() erlaubt wieder Start
};

}  // namespace nl
