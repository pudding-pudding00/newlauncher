#include "platform/wii_input.hpp"

#include <cmath>

#include <gccore.h>
#include <wiiuse/wpad.h>

namespace nl::platform {

namespace {
using B = ui::RawInput::Button;

struct Map {
    u32 from;
    u32 to;
};

constexpr Map kWiimote[] = {
    {WPAD_BUTTON_UP, B::DUp},       {WPAD_BUTTON_DOWN, B::DDown}, {WPAD_BUTTON_LEFT, B::DLeft},
    {WPAD_BUTTON_RIGHT, B::DRight}, {WPAD_BUTTON_A, B::A},        {WPAD_BUTTON_B, B::B},
    {WPAD_BUTTON_MINUS, B::Minus},  {WPAD_BUTTON_PLUS, B::Plus},  {WPAD_BUTTON_1, B::One},
    {WPAD_BUTTON_2, B::Two},        {WPAD_BUTTON_HOME, B::HomeBtn},
};
constexpr Map kGc[] = {
    {PAD_BUTTON_A, B::GcA},         {PAD_BUTTON_B, B::GcB},       {PAD_BUTTON_X, B::GcX},
    {PAD_BUTTON_Y, B::GcY},         {PAD_TRIGGER_L, B::GcL},      {PAD_TRIGGER_R, B::GcR},
    {PAD_TRIGGER_Z, B::GcZ},        {PAD_BUTTON_START, B::GcStart}, {PAD_BUTTON_UP, B::GcUp},
    {PAD_BUTTON_DOWN, B::GcDown},   {PAD_BUTTON_LEFT, B::GcLeft}, {PAD_BUTTON_RIGHT, B::GcRight},
};

template <std::size_t N>
u32 mapButtons(u32 held, const Map (&table)[N]) {
    u32 out = 0;
    for (const Map& m : table)
        if (held & m.from) out |= m.to;
    return out;
}

float clampUnit(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }

// Übernimmt den betragsgrößten Stick aller Geräte.
void takeStick(ui::RawInput& r, float x, float y) {
    if (x * x + y * y > r.stickX * r.stickX + r.stickY * r.stickY) {
        r.stickX = clampUnit(x);
        r.stickY = clampUnit(y);
    }
}
}  // namespace

void WiiInput::init(float logicalWidth, float logicalHeight) {
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, static_cast<u32>(logicalWidth), static_cast<u32>(logicalHeight));
    PAD_Init();
    initialized_ = true;
}

ui::RawInput WiiInput::poll() {
    ui::RawInput r;
    WPAD_ScanPads();
    PAD_ScanPads();
    for (int chan = 0; chan < 4; ++chan) {
        r.held |= mapButtons(WPAD_ButtonsHeld(chan), kWiimote);
        const WPADData* d = WPAD_Data(chan);
        if (d == nullptr) continue;
        if (d->exp.type == WPAD_EXP_NUNCHUK) {
            const float ang = d->exp.nunchuk.js.ang * 3.14159265f / 180.0f;
            const float mag = d->exp.nunchuk.js.mag;
            takeStick(r, mag * std::sin(ang), mag * std::cos(ang));  // 0° = oben, im Uhrzeigersinn
        }
        if (!r.pointerValid && d->ir.valid) {
            r.pointerValid = true;
            r.pointerX = d->ir.x;
            r.pointerY = d->ir.y;
        }
    }
    for (int port = 0; port < 4; ++port) {
        r.held |= mapButtons(PAD_ButtonsHeld(port), kGc);
        takeStick(r, PAD_StickX(port) / 80.0f, PAD_StickY(port) / 80.0f);
    }
    return r;
}

void WiiInput::shutdown() {
    if (!initialized_) return;
    WPAD_Shutdown();
    initialized_ = false;
}

}  // namespace nl::platform
