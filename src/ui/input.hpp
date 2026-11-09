#pragma once
// Eingabe-Aktionen (Spec 3.8): alle Geräte werden auf Aktionen abgebildet, UI-Code kennt nur Aktionen.

#include "core/types.hpp"

namespace nl::ui {

enum class Action : u8 {
    Up, Down, Left, Right, Confirm, Back, PrevConsole, NextConsole,
    Favorite, Search, Home, DebugToggle, Count
};

// Gerätezustand eines Frames, geräteneutral; alle Controller verodert.
struct RawInput {
    enum Button : u32 {
        DUp = 1u << 0, DDown = 1u << 1, DLeft = 1u << 2, DRight = 1u << 3,
        A = 1u << 4, B = 1u << 5, Minus = 1u << 6, Plus = 1u << 7,
        One = 1u << 8, Two = 1u << 9, HomeBtn = 1u << 10,
        GcA = 1u << 11, GcB = 1u << 12, GcX = 1u << 13, GcY = 1u << 14,
        GcL = 1u << 15, GcR = 1u << 16, GcZ = 1u << 17, GcStart = 1u << 18,
        GcUp = 1u << 19, GcDown = 1u << 20, GcLeft = 1u << 21, GcRight = 1u << 22,
    };
    u32 held = 0;
    float stickX = 0.0f, stickY = 0.0f;  // -1..1, +Y = oben
    bool pointerValid = false;
    float pointerX = 0.0f, pointerY = 0.0f;  // logische Koordinaten
};

struct InputFrame {
    u32 actions = 0;
    bool pointerValid = false, pointerMoved = false;
    float pointerX = 0.0f, pointerY = 0.0f;

    bool pressed(Action a) const { return (actions & (1u << static_cast<u32>(a))) != 0; }
};

inline constexpr float kStickDeadZone = 0.35f;
inline constexpr float kRepeatDelayMs = 400.0f;
inline constexpr float kRepeatIntervalMs = 80.0f;
inline constexpr float kPointerMoveThresholdPx = 4.0f;

class InputMapper {
public:
    InputFrame update(const RawInput& raw, float dtMs);

private:
    int direction_ = -1;  // gehaltene Richtung (Action::Up…Right) oder -1
    float heldMs_ = 0.0f, nextRepeatMs_ = 0.0f;
    u32 prevHeld_ = 0;
    bool comboOneTwo_ = false, comboZStart_ = false;
    bool lastPointerValid_ = false;
    float lastPointerX_ = 0.0f, lastPointerY_ = 0.0f;
};

}  // namespace nl::ui
