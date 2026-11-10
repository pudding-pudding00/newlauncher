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

void Audio::shutdown() {
    if (!started_) return;
    ASND_Pause(1);
    ASND_End();  // stoppt den DSP-Mixer, bevor die Puffer freigegeben werden
    started_ = false;
    ready_ = false;
    for (Buffer& b : buffers_) {
        std::free(b.data);
        b = {};
    }
    nl::log::line("NL_AUDIO_STOP");
}

}  // namespace nl::platform
