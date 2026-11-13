#pragma once
// Eingabe-Aktionen (Spec 3.8): alle Geräte werden auf Aktionen abgebildet, UI-Code kennt nur Aktionen.

#include "core/types.hpp"

namespace nl::ui {

enum class Action : u8 {
    Up, Down, Left, Right, Confirm, Back, PrevConsole, NextConsole,
    Favorite, Search, Home, DebugToggle,
    Options,  // Spieloptionen (M14): Wiimote + und GC R, zusätzlich zu NextConsole
    Count
};

// Gerätezustand eines Frames, geräteneutral; alle Controller verodert.
struct RawInput {
    // u64: Classic (M17) belegt die Bits 23–33. Das Classic-Steuerkreuz setzt DUp…DRight.
    enum Button : u64 {
        DUp = 1ull << 0, DDown = 1ull << 1, DLeft = 1ull << 2, DRight = 1ull << 3,
        A = 1ull << 4, B = 1ull << 5, Minus = 1ull << 6, Plus = 1ull << 7,
        One = 1ull << 8, Two = 1ull << 9, HomeBtn = 1ull << 10,
        GcA = 1ull << 11, GcB = 1ull << 12, GcX = 1ull << 13, GcY = 1ull << 14,
        GcL = 1ull << 15, GcR = 1ull << 16, GcZ = 1ull << 17, GcStart = 1ull << 18,
        GcUp = 1ull << 19, GcDown = 1ull << 20, GcLeft = 1ull << 21, GcRight = 1ull << 22,
        CcA = 1ull << 23, CcB = 1ull << 24, CcX = 1ull << 25, CcY = 1ull << 26,
        CcL = 1ull << 27, CcR = 1ull << 28, CcZL = 1ull << 29, CcZR = 1ull << 30,
        CcPlus = 1ull << 31, CcMinus = 1ull << 32, CcHome = 1ull << 33,
    };
    u64 held = 0;
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
    u64 prevHeld_ = 0;
    bool comboOneTwo_ = false, comboZStart_ = false, comboZlZr_ = false;
    bool pointerReported_ = false;
    float lastPointerX_ = 0.0f, lastPointerY_ = 0.0f;
};

}  // namespace nl::ui
