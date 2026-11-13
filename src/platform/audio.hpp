#pragma once
// UI-Klänge über ASND (Spec 3.7): Töne einmal beim Start synthetisieren, dann per Stimme abspielen.

#include <array>

#include "core/bns.hpp"
#include "core/types.hpp"
#include "core/ui_sound.hpp"

namespace nl::platform {

class Audio {
public:
    static constexpr u32 kSampleRate = 48000;

    bool init();                // ASND starten, Töne vorberechnen; loggt NL_AUDIO ok bzw. NL_AUDIO_FAIL
    void play(UiSound s);       // still bei None, ausgeschalteten Klängen oder fehlgeschlagenem init
    void setSounds(bool on) { sounds_ = on; }
    bool sounds() const { return ready_ && sounds_; }
    // Banner-Ton (M18) auf Stimme 4, stereo: loadBanner kopiert die PCM in einen 32-Byte-ausgerichteten
    // Puffer (eine Kopie zugleich); playBanner spielt von vorn, mit Schleife ab loopStart (auf 8 Samples
    // abgerundet, DMA-Ausrichtung). freeBanner stoppt und gibt frei.
    bool loadBanner(const BnsSound& s);
    bool bannerLoaded() const { return banner_.data != nullptr; }
    u32 bannerBytes() const { return banner_.bytes; }
    void playBanner();
    void stopBanner();
    void freeBanner();
    void shutdown();            // idempotent; vor Chainload, Kanalstart, Wii-Menü und Beenden; loggt NL_AUDIO_STOP

private:
    struct Buffer {
        s16* data = nullptr;
        u32 bytes = 0;
    };
    static constexpr int kSoundCount = static_cast<int>(UiSound::Error) + 1;
    std::array<Buffer, kSoundCount> buffers_{};
    Buffer banner_{};
    u32 bannerRate_ = 0, bannerLoopAt_ = 0;  // Schleifenstart in Byte, 0 ohne Schleife
    bool bannerLoop_ = false;
    bool started_ = false;  // ASND_Init lief, ASND_End steht aus
    bool ready_ = false;    // Puffer vollständig
    bool sounds_ = true;
    int nextVoice_ = 0;
};

}  // namespace nl::platform
