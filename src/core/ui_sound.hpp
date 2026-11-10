#pragma once
// UI-Klänge (Spec 3.7): kurze synthetisierte Töne für Fokus, Bestätigen, Zurück, Öffnen und Fehler.

#include "core/types.hpp"

namespace nl {

enum class UiSound : u8 { None, Move, Confirm, Back, Open, Error };

const char* uiSoundName(UiSound s);  // "move", "confirm", "back", "open", "error"; None → "none"

// Sinus mit linearem Frequenzverlauf von startHz nach endHz über ms Millisekunden.
struct ToneSpec {
    float startHz, endHz;
    u16 ms;
    float amplitude;  // 0..1 relativ zu 32767
};

ToneSpec uiSoundSpec(UiSound s);  // None → Dauer 0

}  // namespace nl
