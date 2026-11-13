#include "platform/audio.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <vector>

#include <asndlib.h>
#include <gccore.h>

#include "core/synth.hpp"
#include "platform/log.hpp"

namespace nl::platform {

namespace {
// Stimme 0 bleibt der Musik (M6 Task 7); UI-Klänge wechseln reihum über 1..3, damit schnelle
// Fokuswechsel den vorigen Ton nicht abschneiden.
constexpr int kFirstUiVoice = 1;
constexpr int kUiVoices = 3;
constexpr int kBannerVoice = 4;

// Schleife des Banner-Tons: ASND ruft den Rückruf (im Interrupt), sobald der zweite Puffer frei ist; dann
// wird der Schleifenteil erneut angehängt. Nur eine Banner-Stimme, daher dateiweite Werte.
s16* volatile gLoopData = nullptr;
volatile u32 gLoopBytes = 0;

void loopCallback(s32 voice) {
    s16* const data = gLoopData;
    if (data != nullptr && ASND_TestVoiceBufferReady(voice)) ASND_AddVoice(voice, data, static_cast<s32>(gLoopBytes));
}
}  // namespace

bool Audio::init() {
    if (started_) return ready_;
    ASND_Init();
    ASND_Pause(0);
    started_ = true;
    for (int i = 1; i < kSoundCount; ++i) {
        const std::vector<s16> pcm = synthTone(uiSoundSpec(static_cast<UiSound>(i)), kSampleRate);
        const u32 bytes = static_cast<u32>(pcm.size() * sizeof(s16));
        if (bytes == 0) continue;
        void* mem = memalign(32, bytes);  // DMA: 32-Byte-ausgerichtet, Länge ist ein Vielfaches von 32
        if (mem == nullptr) {
            nl::log::line("NL_AUDIO_FAIL kein Speicher fuer %s", uiSoundName(static_cast<UiSound>(i)));
            shutdown();
            return false;
        }
        std::memcpy(mem, pcm.data(), bytes);  // PPC ist Big-Endian: passt zu VOICE_MONO_16BIT
        DCFlushRange(mem, bytes);
        buffers_[static_cast<std::size_t>(i)] = {static_cast<s16*>(mem), bytes};
    }
    ready_ = true;
    nl::log::line("NL_AUDIO ok");
    return true;
}

void Audio::play(UiSound s) {
    if (!ready_ || !sounds_ || s == UiSound::None) return;
    const Buffer& b = buffers_[static_cast<std::size_t>(s)];
    if (b.data == nullptr) return;
    const int voice = kFirstUiVoice + nextVoice_;
    nextVoice_ = (nextVoice_ + 1) % kUiVoices;
    ASND_SetVoice(voice, VOICE_MONO_16BIT, static_cast<s32>(kSampleRate), 0, b.data, static_cast<s32>(b.bytes), 255,
                  255, nullptr);
}

bool Audio::loadBanner(const BnsSound& s) {
    freeBanner();
    if (!ready_ || s.pcm.empty()) return false;
    const u32 raw = static_cast<u32>(s.pcm.size() * sizeof(s16));
    const u32 bytes = (raw + 31) & ~31u;  // DMA: Länge ein Vielfaches von 32, Rest Stille
    void* mem = memalign(32, bytes);
    if (mem == nullptr) return false;
    std::memcpy(mem, s.pcm.data(), raw);  // Big-Endian, verschränkt L/R wie VOICE_STEREO_16BIT
    std::memset(static_cast<u8*>(mem) + raw, 0, bytes - raw);
    DCFlushRange(mem, bytes);
    banner_ = {static_cast<s16*>(mem), bytes};
    bannerRate_ = s.sampleRate;
    bannerLoop_ = s.loop;
    bannerLoopAt_ = s.loop ? (s.loopStart & ~7u) * 4 : 0;  // 8 Stereo-Samples = 32 Byte
    return true;
}

void Audio::playBanner() {
    if (!ready_ || !sounds_ || banner_.data == nullptr) return;
    stopBanner();
    gLoopBytes = banner_.bytes - bannerLoopAt_;
    gLoopData = bannerLoop_ ? banner_.data + bannerLoopAt_ / sizeof(s16) : nullptr;
    ASND_SetVoice(kBannerVoice, VOICE_STEREO_16BIT, static_cast<s32>(bannerRate_), 0, banner_.data,
                  static_cast<s32>(banner_.bytes), 255, 255, bannerLoop_ ? loopCallback : nullptr);
}

bool Audio::bannerVoiceIdle() const { return started_ && ASND_StatusVoice(kBannerVoice) == SND_UNUSED; }

void Audio::stopBanner() {
    gLoopData = nullptr;  // zuerst: der Rückruf hängt danach nichts mehr an
    if (started_) ASND_StopVoice(kBannerVoice);
}

void Audio::freeBanner() {
    stopBanner();
    std::free(banner_.data);
    banner_ = {};
}

void Audio::shutdown() {
    if (!started_) return;
    ASND_Pause(1);
    gLoopData = nullptr;
    ASND_End();  // stoppt den DSP-Mixer, bevor die Puffer freigegeben werden
    std::free(banner_.data);
    banner_ = {};
    started_ = false;
    ready_ = false;
    for (Buffer& b : buffers_) {
        std::free(b.data);
        b = {};
    }
    nl::log::line("NL_AUDIO_STOP");
}

}  // namespace nl::platform
