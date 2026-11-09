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

constexpr int kIdleStart = 100, kIdleEnd = 220;

ui::RawInput press(u32 bits) {
    ui::RawInput r;
    r.held = bits;
    return r;
}
}  // namespace

ui::RawInput selftestInput(int frame, const SelftestView& v) {
    switch (frame) {
        case 30: case 32: return press(B::Plus);      // Favoriten → Zuletzt → NES
        case 34: return press(B::DRight);
        case 36: return press(B::DDown);
        case 40: {                                     // Zeiger auf Kachel 2
            const gfx::Rect r = v.main.grid().tileRect(2, 0);
            ui::RawInput in;
            in.pointerValid = true;
            in.pointerX = r.x + r.w / 2;
            in.pointerY = r.y + r.h / 2;
            return in;
        }
        case 45: return press(B::HomeBtn);
        case 60: return press(B::B);
        case 85: return press(B::One | B::Two);
        default: return ui::RawInput{};
    }
}

void selftestAfterFrame(int frame, const SelftestView& v) {
    switch (frame) {
        case 33:
            NL_ASSERT(v.main.barIndex() == 2);
            NL_ASSERT(v.main.focusIndex() == 0);
            break;
        case 37: NL_ASSERT(v.main.focusIndex() == 7); break;
        case 41: NL_ASSERT(v.main.focusIndex() == 2); break;
        case 46: NL_ASSERT(v.scenes.depth() == 2); break;
        case 80: NL_ASSERT(v.scenes.depth() == 1); break;
        case 86: NL_ASSERT(v.debug.visible()); break;
        case kIdleStart: g_allocs = 0; break;
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
