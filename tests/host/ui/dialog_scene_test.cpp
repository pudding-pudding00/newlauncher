#include "ui/dialog_scene.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
using namespace nl;
using namespace nl::ui;

// Zählt Heap-Allokationen im ganzen Testprogramm; die Tests lesen nur die Differenz.
namespace {
std::atomic<long> gAllocs{0};
}
void* operator new(std::size_t n) {
    ++gAllocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{4096, 16};
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame pointerAt(float x, float y, bool confirm) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = true;
        f.pointerX = x;
        f.pointerY = y;
        if (confirm) f.actions = 1u << static_cast<u32>(Action::Confirm);
        return f;
    }
    SceneRequests step(DialogScene& s, const InputFrame& in) {
        SceneRequests r;
        s.update(ctx, in, 16, r);
        return r;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Dialog: A schließt (pop)") {
    DialogScene d;
    d.open("Start nicht möglich", "Emulator nicht gefunden:\nsd:/apps/x/boot.dol");
    CHECK(d.isOverlay());
    CHECK_FALSE(step(d, InputFrame{}).pop);
    CHECK(step(d, action(Action::Confirm)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Dialog: B schließt") {
    DialogScene d;
    d.open("T", "x");
    CHECK(step(d, action(Action::Back)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Dialog: Klick auf OK-Rechteck schließt, Klick daneben nicht") {
    DialogScene d;
    d.open("T", "x");
    const gfx::Rect r = d.buttonRect(ctx);
    CHECK_FALSE(step(d, pointerAt(r.x - 40, r.y - 40, true)).pop);
    CHECK(step(d, pointerAt(r.x + r.w / 2, r.y + r.h / 2, true)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Dialog: lange Texte werden gekürzt, Titel und Text bleiben erreichbar") {
    DialogScene d;
    const std::string longText(1000, 'y');
    d.open(longText.c_str(), longText.c_str());
    CHECK(std::string(d.title()).size() < 64);
    CHECK(std::string(d.text()).size() < 256);
    CHECK(std::string(d.text()).substr(std::string(d.text()).size() - 3) == "\xE2\x80\xA6");
}

TEST_CASE_FIXTURE(Fixture, "Dialog: update/draw allokieren nicht") {
    DialogScene d;
    d.open("Start nicht möglich", "Spieldatei nicht gefunden:\nsd:/roms/nes/Ein sehr langer Name mit Leerzeichen (Europe) (Rev 1).nes");
    step(d, InputFrame{});
    d.draw(ctx, dl);
    dl.clear();
    const long before = gAllocs.load();
    for (int i = 0; i < 10; ++i) {
        step(d, pointerAt(10, 10, false));
        d.draw(ctx, dl);
        dl.clear();
    }
    CHECK(gAllocs.load() == before);
    { std::string probe(100, 'x'); }  // der Zähler läuft wirklich
    CHECK(gAllocs.load() > before);
}
