#include "app/selftest.hpp"

#include <cstddef>
#include <reent.h>

#include "core/assert.hpp"
#include "platform/log.hpp"

// Allokationszähler: das Ziel nl_selftest linkt mit --wrap für _malloc_r, _memalign_r und
// _realloc_r. In libogc 3 springt malloc() bei Größen über 63 Byte direkt zu _memalign_r,
// deshalb reicht _malloc_r allein nicht.
namespace {
volatile unsigned g_allocs = 0;
}

extern "C" void* __real__malloc_r(struct _reent* r, std::size_t n);
extern "C" void* __real__memalign_r(struct _reent* r, std::size_t align, std::size_t n);
extern "C" void* __real__realloc_r(struct _reent* r, void* p, std::size_t n);

extern "C" void* __wrap__malloc_r(struct _reent* r, std::size_t n) {
    g_allocs = g_allocs + 1;
    return __real__malloc_r(r, n);
}
extern "C" void* __wrap__memalign_r(struct _reent* r, std::size_t align, std::size_t n) {
    g_allocs = g_allocs + 1;
    return __real__memalign_r(r, align, n);
}
extern "C" void* __wrap__realloc_r(struct _reent* r, void* p, std::size_t n) {
    g_allocs = g_allocs + 1;
    return __real__realloc_r(r, p, n);
}

namespace nl::app {

namespace {
using B = ui::RawInput::Button;

// Ablauf (Frames ab dem ersten Hauptbildschirm-Frame; jede Taste: drücken, im nächsten Frame loslassen).
// Fixture-Leiste: 0 Favoriten, 1 Zuletzt, 2 Wii, 3 GC, 4 GBA, 5 N64, 6 SNES, 7 GB, 8 NES.
constexpr int kTypeStart = 182;          // Suche: "MARIO" per Zeiger tippen, je Taste 3 Frames
constexpr const char* kTyped = "MARIO";
constexpr int kIdleStart = 250, kIdleEnd = 370;

ui::RawInput press(u32 bits) {
    ui::RawInput r;
    r.held = bits;
    return r;
}

ui::RawInput pointerAt(const gfx::Rect& r, bool pressA) {
    ui::RawInput in;
    in.pointerValid = true;
    in.pointerX = r.x + r.w / 2;
    in.pointerY = r.y + r.h / 2;
    if (pressA) in.held = B::A;
    return in;
}

bool isEven(int f) { return (f & 1) == 0; }
}  // namespace

ui::RawInput selftestInput(int frame, const SelftestView& v) {
    // M1-Teil: Leiste, Fokus, Zeiger, HOME, Debug
    switch (frame) {
        case 30: case 32: return press(B::Plus);      // Favoriten → Zuletzt → Wii (2 Spiele)
        case 34: return press(B::DRight);
        case 36: return press(B::DDown);
        case 40: return pointerAt(v.main.grid().tileRect(0, 0), false);
        case 45: return press(B::HomeBtn);
        case 60: return press(B::B);
        case 85: return press(B::One | B::Two);
        // M2: N64 wählen, Favorit setzen
        case 90: case 92: case 94: return press(B::Plus);  // Wii → GC → GBA → N64
        case 96: return press(B::One);                      // Favorit (wirkt beim Loslassen)
        // A–Z-Leiste: Taste 2, von "M" sechsmal nach rechts auf "S", bestätigen
        case 100: return press(B::Two);
        case 116: return press(B::A);
        // Suche: Taste 2, von "S" achtmal nach rechts auf „Suche", bestätigen → Tastatur
        case 140: return press(B::Two);
        case 158: return press(B::A);
        // Tastatur schließen, Suchmodus beenden, Detailansicht öffnen und schließen
        case 200: return press(B::B);
        case 216: return press(B::B);
        case 220: return press(B::A);
        case 236: return press(B::B);
        default: break;
    }
    if (frame >= 104 && frame <= 114 && isEven(frame)) return press(B::DRight);
    if (frame >= 142 && frame <= 156 && isEven(frame)) return press(B::DRight);
    if (frame >= kTypeStart && frame < kTypeStart + 15) {
        const int i = (frame - kTypeStart) / 3, phase = (frame - kTypeStart) % 3;
        const gfx::Rect r = v.keyboard.keyRect(v.ctx, v.keyboard.keyIndex(kTyped[i]));
        if (phase == 0) return pointerAt(r, false);  // hinzeigen (Bewegung ≥ 4 px)
        if (phase == 1) return pointerAt(r, true);   // A drücken
        return pointerAt(r, false);                  // loslassen
    }
    return ui::RawInput{};
}

void selftestAfterFrame(int frame, const SelftestView& v) {
    switch (frame) {
        case 33:
            NL_ASSERT(v.main.barIndex() == 2);
            NL_ASSERT(v.main.focusIndex() == 0);
            break;
        case 37: NL_ASSERT(v.main.focusIndex() == 1); break;  // Rechts → 1, Runter bleibt (nur eine Zeile)
        case 41: NL_ASSERT(v.main.focusIndex() == 0); break;
        case 46: NL_ASSERT(v.scenes.depth() == 2); break;
        case 80: NL_ASSERT(v.scenes.depth() == 1); break;
        case 86: NL_ASSERT(v.debug.visible()); break;
        case 95:
            NL_ASSERT(v.main.barIndex() == 5);
            NL_ASSERT(v.main.focusIndex() == 0);
            break;
        case 99: NL_ASSERT(v.store.favorites().size() == 3); break;
        case 102: NL_ASSERT(v.scenes.depth() == 2); break;  // A–Z-Leiste offen
        case 132:
            NL_ASSERT(v.scenes.depth() == 1);
            NL_ASSERT(v.main.focusIndex() == 1);  // „Super Mario 64"
            break;
        case 180: NL_ASSERT(v.scenes.depth() == 2); break;  // Tastatur offen
        case 198:
            NL_ASSERT(v.keyboard.text() == kTyped);
            NL_ASSERT(v.main.searchActive());
            NL_ASSERT(v.main.barIndex() == 0);
            NL_ASSERT(v.main.grid().count() == 5);
            log::line("NL_SEARCH mario=%d", v.main.grid().count());
            break;
        case 215: NL_ASSERT(v.scenes.depth() == 1); break;
        case 218:
            NL_ASSERT(!v.main.searchActive());
            NL_ASSERT(v.main.barIndex() == 5);
            break;
        case 222: NL_ASSERT(v.scenes.depth() == 2); break;  // Detailansicht
        case 240: NL_ASSERT(v.scenes.depth() == 1); break;
        case kIdleStart:
            NL_ASSERT(!v.scanRunning);
            g_allocs = 0;
            break;
        case kIdleEnd: {
            const unsigned allocs = g_allocs;
            log::line("NL_ALLOC_IDLE %u", allocs);
            NL_ASSERT(allocs == 0);
            log::line("NL_SELFTEST_OK");
            __builtin_trap();  // erwartet: Absturzbildschirm mit NL_CRASH id=7
        }
        default: break;
    }
}

}  // namespace nl::app
