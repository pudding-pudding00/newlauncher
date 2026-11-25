#include "platform/wii_input.hpp"

#include <cmath>

#include <bte/bte.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "core/battery.hpp"

namespace nl::platform {

namespace {
using B = ui::RawInput::Button;

struct Map {
    u32 from;
    u64 to;
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

// Classic Controller (auch Classic Pro und Wii-U-Pro, die libogc ebenfalls als WPAD_EXP_CLASSIC
// meldet) in den oberen 16 Bit von WPAD_ButtonsHeld. Nunchuk und Gitarre nutzen dieselben Bits,
// deshalb gilt die Tabelle nur bei exp.type == WPAD_EXP_CLASSIC.
constexpr Map kClassic[] = {
    {WPAD_CLASSIC_BUTTON_UP, B::DUp},       {WPAD_CLASSIC_BUTTON_DOWN, B::DDown},
    {WPAD_CLASSIC_BUTTON_LEFT, B::DLeft},   {WPAD_CLASSIC_BUTTON_RIGHT, B::DRight},
    {WPAD_CLASSIC_BUTTON_A, B::CcA},        {WPAD_CLASSIC_BUTTON_B, B::CcB},
    {WPAD_CLASSIC_BUTTON_X, B::CcX},        {WPAD_CLASSIC_BUTTON_Y, B::CcY},
    {WPAD_CLASSIC_BUTTON_FULL_L, B::CcL},   {WPAD_CLASSIC_BUTTON_FULL_R, B::CcR},
    {WPAD_CLASSIC_BUTTON_ZL, B::CcZL},      {WPAD_CLASSIC_BUTTON_ZR, B::CcZR},
    {WPAD_CLASSIC_BUTTON_PLUS, B::CcPlus},  {WPAD_CLASSIC_BUTTON_MINUS, B::CcMinus},
    {WPAD_CLASSIC_BUTTON_HOME, B::CcHome},
};

template <std::size_t N>
u64 mapButtons(u32 held, const Map (&table)[N]) {
    u64 out = 0;
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
// Analogstick von libwiiuse: Winkel 0° = oben, im Uhrzeigersinn; Betrag 0..1.
void takeJoystick(ui::RawInput& r, const joystick_t& js) {
    const float ang = js.ang * 3.14159265f / 180.0f;
    takeStick(r, js.mag * std::sin(ang), js.mag * std::cos(ang));
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
        const u32 wpadHeld = WPAD_ButtonsHeld(chan);
        r.held |= mapButtons(wpadHeld, kWiimote);
        const WPADData* d = WPAD_Data(chan);
        if (d == nullptr) continue;
        if (d->exp.type == WPAD_EXP_NUNCHUK) {
            takeJoystick(r, d->exp.nunchuk.js);
        } else if (d->exp.type == WPAD_EXP_CLASSIC) {
            // Wird der Classic abgesteckt, meldet der nächste Frame einen anderen Typ: die
            // gehaltenen Classic-Bits fallen mit ihm weg, nichts bleibt hängen.
            r.held |= mapButtons(wpadHeld, kClassic);
            takeJoystick(r, d->exp.classic.ljs);
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
    if (pollCount_++ % 60 == 0) {
        for (int chan = 0; chan < 4; ++chan) {
            u32 type = 0;
            const WPADData* d = WPAD_Data(chan);
            battery_[static_cast<std::size_t>(chan)] =
                WPAD_Probe(chan, &type) == WPAD_ERR_NONE && d != nullptr ? static_cast<s8>(batteryBars(d->battery_level)) : -1;
        }
    }
    return r;
}

void WiiInput::shutdown() {
    if (!initialized_) return;
    // libogc 3.2.1 WPAD_Shutdown gibt __wpads frei und aktiviert IRQs vor BTE_Shutdown: trifft dazwischen ein
    // BT-Paket ein, dereferenziert __wpad_eventCB das freigegebene wm (DSI). BTE zuerst stoppen, solange
    // __wpads noch gültig ist; das zweite BTE_Shutdown in WPAD_Shutdown wird dann übersprungen.
    BTE_Shutdown();
    WPAD_Shutdown();
    initialized_ = false;
}

}  // namespace nl::platform
