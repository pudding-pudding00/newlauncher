#include "ui/input.hpp"

namespace nl::ui {

namespace {
using B = RawInput::Button;

u32 bit(Action a) { return 1u << static_cast<u32>(a); }

float absf(float v) { return v < 0.0f ? -v : v; }

int directionOf(const RawInput& r) {
    const u64 h = r.held;
    if (h & (B::DUp | B::GcUp)) return static_cast<int>(Action::Up);
    if (h & (B::DDown | B::GcDown)) return static_cast<int>(Action::Down);
    if (h & (B::DLeft | B::GcLeft)) return static_cast<int>(Action::Left);
    if (h & (B::DRight | B::GcRight)) return static_cast<int>(Action::Right);
    const float ax = absf(r.stickX), ay = absf(r.stickY);
    if (ax < kStickDeadZone && ay < kStickDeadZone) return -1;
    if (ax >= ay) return static_cast<int>(r.stickX > 0.0f ? Action::Right : Action::Left);
    return static_cast<int>(r.stickY > 0.0f ? Action::Up : Action::Down);
}
}  // namespace

InputFrame InputMapper::update(const RawInput& raw, float dtMs) {
    InputFrame f;
    if (dtMs < 0.0f) dtMs = 0.0f;
    const u64 held = raw.held;
    const u64 down = held & ~prevHeld_;
    const u64 up = prevHeld_ & ~held;

    // Richtungen mit Wiederholung: sofort, nach 400 ms, dann alle 80 ms; höchstens einmal pro Frame.
    const int dir = directionOf(raw);
    if (dir != direction_) {
        direction_ = dir;
        heldMs_ = 0.0f;
        nextRepeatMs_ = kRepeatDelayMs;
        if (dir >= 0) f.actions |= 1u << static_cast<u32>(dir);
    } else if (dir >= 0) {
        heldMs_ += dtMs;
        if (heldMs_ >= nextRepeatMs_) {
            f.actions |= 1u << static_cast<u32>(dir);
            nextRepeatMs_ += kRepeatIntervalMs;
            if (nextRepeatMs_ <= heldMs_) nextRepeatMs_ = heldMs_ + kRepeatIntervalMs;
        }
    }

    if (down & (B::A | B::GcA | B::CcA)) f.actions |= bit(Action::Confirm);
    if (down & (B::B | B::GcB | B::CcB)) f.actions |= bit(Action::Back);
    if (down & (B::Minus | B::GcL | B::CcL)) f.actions |= bit(Action::PrevConsole);
    if (down & (B::Plus | B::GcR)) f.actions |= bit(Action::NextConsole) | bit(Action::Options);
    if (down & B::CcR) f.actions |= bit(Action::NextConsole);
    if (down & B::CcPlus) f.actions |= bit(Action::Options);  // Classic hat L/R zum Wechseln
    if (down & (B::GcY | B::CcY)) f.actions |= bit(Action::Favorite);
    if (down & (B::GcX | B::CcX)) f.actions |= bit(Action::Search);
    if (down & (B::HomeBtn | B::CcHome)) f.actions |= bit(Action::Home);

    // Kombinationen: 1+2 bzw. Z+Start lösen einmal DebugToggle aus; die Einzeltasten wirken
    // erst beim Loslassen und entfallen, wenn die Kombination gehalten wurde.
    const bool oneTwo = (held & B::One) && (held & B::Two);
    if (oneTwo && !comboOneTwo_) {
        comboOneTwo_ = true;
        f.actions |= bit(Action::DebugToggle);
    }
    const bool zStart = (held & B::GcZ) && (held & B::GcStart);
    if (zStart && !comboZStart_) {
        comboZStart_ = true;
        f.actions |= bit(Action::DebugToggle);
    }
    // Classic ZL+ZR: ZL und ZR sind einzeln unbelegt, deshalb kein Verzögern der Einzeltasten.
    const bool zlZr = (held & B::CcZL) && (held & B::CcZR);
    if (zlZr && !comboZlZr_) {
        comboZlZr_ = true;
        f.actions |= bit(Action::DebugToggle);
    }
    if (!comboOneTwo_) {
        if (up & B::One) f.actions |= bit(Action::Favorite);
        if (up & B::Two) f.actions |= bit(Action::Search);
    }
    if (!comboZStart_ && (up & B::GcStart)) f.actions |= bit(Action::Home);
    if (!(held & (B::One | B::Two))) comboOneTwo_ = false;
    if (!(held & (B::GcZ | B::GcStart))) comboZStart_ = false;
    if (!(held & (B::CcZL | B::CcZR))) comboZlZr_ = false;
    prevHeld_ = held;

    // Zeiger: „bewegt" erst ab 4 px Abstand zur zuletzt gemeldeten Position.
    f.pointerValid = raw.pointerValid;
    if (raw.pointerValid) {
        f.pointerX = raw.pointerX;
        f.pointerY = raw.pointerY;
        // Die zuletzt gemeldete Position bleibt über IR-Aussetzer erhalten: kehrt der Zeiger an
        // dieselbe Stelle zurück, ist das keine Bewegung und stiehlt keinen Tastenfokus.
        const float dx = raw.pointerX - lastPointerX_, dy = raw.pointerY - lastPointerY_;
        if (!pointerReported_ || dx * dx + dy * dy >= kPointerMoveThresholdPx * kPointerMoveThresholdPx) {
            f.pointerMoved = true;
            pointerReported_ = true;
            lastPointerX_ = raw.pointerX;
            lastPointerY_ = raw.pointerY;
        }
    }
    return f;
}

}  // namespace nl::ui
