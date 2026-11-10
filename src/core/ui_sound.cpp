#include "core/ui_sound.hpp"

namespace nl {

const char* uiSoundName(UiSound s) {
    switch (s) {
        case UiSound::Move: return "move";
        case UiSound::Confirm: return "confirm";
        case UiSound::Back: return "back";
        case UiSound::Open: return "open";
        case UiSound::Error: return "error";
        case UiSound::None: break;
    }
    return "none";
}

ToneSpec uiSoundSpec(UiSound s) {
    switch (s) {
        case UiSound::Move: return {1200.0f, 1200.0f, 25, 0.20f};
        case UiSound::Confirm: return {880.0f, 1320.0f, 70, 0.30f};
        case UiSound::Back: return {660.0f, 440.0f, 70, 0.30f};
        case UiSound::Open: return {523.0f, 784.0f, 110, 0.25f};
        case UiSound::Error: return {220.0f, 196.0f, 160, 0.35f};
        case UiSound::None: break;
    }
    return {0.0f, 0.0f, 0, 0.0f};
}

}  // namespace nl
