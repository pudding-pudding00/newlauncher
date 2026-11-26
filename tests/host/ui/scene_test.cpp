#include "ui/scene.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <cstring>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct Recorder final : Scene {
    explicit Recorder(bool overlay = false) : overlay_(overlay) {}
    void update(const UiContext&, const InputFrame& in, float, SceneRequests& out) override {
        ++updates;
        if (in.actions != 0) ++inputs;
        out = next;
        next = SceneRequests{};
    }
    void draw(const UiContext&, gfx::DrawList& dl) override {
        ++draws;
        dl.quad(0, gfx::Rect{0, 0, 1, 1}, gfx::UvRect{0, 0, 1, 1}, Color{9, 9, 9, 255});
    }
    bool isOverlay() const override { return overlay_; }
    void onEnter() override { ++enters; }

    bool overlay_;
    int updates = 0, inputs = 0, draws = 0, enters = 0;
    SceneRequests next{};
};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet();
    UiContext ctx;
    gfx::DrawList dl{256, 8};
    Fixture() {
        ctx.theme = &darkTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
    }
    InputFrame key() const {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(Action::Confirm);
        return f;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "update und draw erreichen die einzige Szene") {
    SceneStack st;
    Recorder a;
    st.push(&a);
    CHECK(a.enters == 1);
    st.update(ctx, key(), 16);
    st.draw(ctx, dl);
    CHECK(a.updates == 1);
    CHECK(a.inputs == 1);
    CHECK(a.draws == 1);
}

TEST_CASE_FIXTURE(Fixture, "Overlay: darunterliegende Szene wird gezeichnet, bekommt aber keine Eingaben") {
    SceneStack st;
    Recorder base, over(true);
    st.push(&base);
    st.push(&over);
    st.update(ctx, key(), 16);
    st.draw(ctx, dl);
    CHECK(base.updates == 1);
    CHECK(base.inputs == 0);
    CHECK(base.draws == 1);
    CHECK(over.inputs == 1);
    CHECK(over.draws == 1);
}

TEST_CASE_FIXTURE(Fixture, "Vollbild-Szene obenauf: darunterliegende wird weder gezeichnet noch aktualisiert") {
    SceneStack st;
    Recorder base, full;
    st.push(&base);
    st.push(&full);
    st.update(ctx, key(), 16);
    st.draw(ctx, dl);
    CHECK(base.updates == 0);
    CHECK(base.draws == 0);
    CHECK(full.draws == 1);
}

TEST_CASE_FIXTURE(Fixture, "push-Anforderung einer Szene legt die neue Szene auf den Stapel und ruft onEnter") {
    SceneStack st;
    Recorder base, over(true);
    st.push(&base);
    base.next.push = &over;
    st.update(ctx, InputFrame{}, 16);
    CHECK(st.depth() == 2);
    CHECK(st.top() == &over);
    CHECK(over.enters == 1);
}

TEST_CASE_FIXTURE(Fixture, "pop blendet 200 ms aus und entfernt die Szene erst danach") {
    SceneStack st;
    Recorder base, over(true);
    st.push(&base);
    st.push(&over);
    st.update(ctx, InputFrame{}, 200);  // Einblendung abgeschlossen
    over.next.pop = true;
    st.update(ctx, InputFrame{}, 16);
    st.update(ctx, InputFrame{}, 100);
    CHECK(st.depth() == 2);
    st.update(ctx, InputFrame{}, 100);
    CHECK(st.depth() == 1);
    CHECK(st.top() == &base);
}

TEST_CASE_FIXTURE(Fixture, "während des Ausblendens erhält keine Szene Eingaben") {
    SceneStack st;
    Recorder base, over(true);
    st.push(&base);
    st.push(&over);
    over.next.pop = true;
    st.update(ctx, InputFrame{}, 16);
    const int overInputs = over.inputs;
    st.update(ctx, key(), 50);
    CHECK(over.inputs == overInputs);
    CHECK(base.inputs == 0);
}

TEST_CASE_FIXTURE(Fixture, "Overlay-Abdunkelung folgt ease-out") {
    SceneStack st;
    Recorder base, over(true);
    st.push(&base);
    st.push(&over);
    st.update(ctx, InputFrame{}, 100);
    st.draw(ctx, dl);
    // Reihenfolge: base-Quad, Abdunkelung, Overlay-Quad.
    REQUIRE(dl.quadCount() == 3);
    const u8 dimAlpha = dl.vertices()[4].color.a;
    const float expected = darkTheme().overlayDim.a * 0.875f;
    CHECK(dimAlpha >= expected - 1.0f);
    CHECK(dimAlpha <= expected + 1.0f);
    CHECK(dl.vertices()[8].color.a < 255);  // Overlay-Inhalt halb eingeblendet
}

TEST_CASE_FIXTURE(Fixture, "AppCommand der obersten Szene wird zurückgegeben") {
    SceneStack st;
    Recorder a;
    st.push(&a);
    a.next.command = AppCommand::PowerOff;
    CHECK(st.update(ctx, InputFrame{}, 16) == AppCommand::PowerOff);
    CHECK(st.update(ctx, InputFrame{}, 16) == AppCommand::None);
    CHECK(std::strcmp(appCommandName(AppCommand::ExitToHbc), "exit_hbc") == 0);
    CHECK(std::strcmp(appCommandName(AppCommand::Launch), "launch") == 0);
}

TEST_CASE_FIXTURE(Fixture, "pop auf der letzten Szene wird ignoriert") {
    SceneStack st;
    Recorder a;
    st.push(&a);
    a.next.pop = true;
    st.update(ctx, InputFrame{}, 16);
    st.update(ctx, InputFrame{}, 500);
    CHECK(st.depth() == 1);
}

TEST_CASE_FIXTURE(Fixture, "push über Tiefe 6 hinaus wird ignoriert") {
    SceneStack st;
    Recorder s[7] = {Recorder(),     Recorder(true), Recorder(true), Recorder(true),
                     Recorder(true), Recorder(true), Recorder(true)};
    for (auto& r : s) st.push(&r);
    CHECK(st.depth() == 6);
    CHECK(s[6].enters == 0);
}

TEST_CASE_FIXTURE(Fixture, "leerer Stapel: update liefert None, draw zeichnet nichts") {
    SceneStack st;
    CHECK(st.update(ctx, key(), 16) == AppCommand::None);
    st.draw(ctx, dl);
    CHECK(dl.quadCount() == 0);
    CHECK(st.top() == nullptr);
}

TEST_CASE_FIXTURE(Fixture, "update reicht die Anforderung der obersten Szene samt Spiel heraus") {
    SceneStack st;
    Recorder a;
    st.push(&a);
    const Game g{Console::Nes, "nes/a", "A", 0, 0};
    a.next.command = AppCommand::ToggleFavorite;
    a.next.game = &g;
    SceneRequests out;
    CHECK(st.update(ctx, InputFrame{}, 16, &out) == AppCommand::ToggleFavorite);
    CHECK(out.game == &g);
    CHECK(st.update(ctx, InputFrame{}, 16, &out) == AppCommand::None);
    CHECK(out.game == nullptr);
}

TEST_CASE_FIXTURE(Fixture, "pop und push in einer Anforderung: erst ausblenden, dann die neue Szene") {
    SceneStack st;
    Recorder base, over(true), next(true);
    st.push(&base);
    st.push(&over);
    st.update(ctx, InputFrame{}, 200);
    over.next.pop = true;
    over.next.push = &next;
    st.update(ctx, InputFrame{}, 16);
    CHECK(st.top() == &over);  // blendet noch aus
    st.update(ctx, InputFrame{}, 100);
    CHECK(next.enters == 0);
    st.update(ctx, InputFrame{}, 100);
    CHECK(st.top() == &next);
    CHECK(next.enters == 1);
    CHECK(st.depth() == 2);
}

TEST_CASE("appCommandName für die M4-Befehle") {
    CHECK(std::strcmp(appCommandName(AppCommand::SetN64Method), "set_n64_method") == 0);
    CHECK(std::strcmp(appCommandName(AppCommand::OpenVcMap), "open_vc_map") == 0);
    CHECK(std::strcmp(appCommandName(AppCommand::SaveVcMap), "save_vc_map") == 0);
}

TEST_CASE_FIXTURE(Fixture, "push von außen, während ein Overlay ausblendet, wartet bis es weg ist") {
    SceneStack st;
    Recorder base, over(true), dialog;
    st.push(&base);
    st.push(&over);
    st.update(ctx, InputFrame{}, 200);
    over.next.pop = true;  // z. B. HOME-Menü: OpenVcMap + pop
    st.update(ctx, InputFrame{}, 16);
    over.next = SceneRequests{};
    st.push(&dialog);  // die App öffnet den Dialog im selben Frame
    CHECK(st.top() == &over);
    CHECK(dialog.enters == 0);
    st.update(ctx, InputFrame{}, 200);
    CHECK(st.top() == &dialog);
    CHECK(dialog.enters == 1);
    CHECK(st.depth() == 2);
}

TEST_CASE_FIXTURE(Fixture, "push ignoriert eine Szene, die schon auf dem Stapel liegt") {
    SceneStack st;
    Recorder a, d(true), other(true);
    st.push(&a);
    st.push(&d);
    st.push(&d);
    CHECK(st.depth() == 2);
    CHECK(d.enters == 1);
    CHECK(st.contains(&a));
    CHECK(st.contains(&d));
    CHECK_FALSE(st.contains(&other));
    CHECK_FALSE(st.contains(nullptr));
}

TEST_CASE_FIXTURE(Fixture, "contains sieht auch die Szene, die nach dem Ausblenden folgt") {
    SceneStack st;
    Recorder a, d(true), next(true);
    st.push(&a);
    st.push(&d);
    d.next.pop = true;
    d.next.push = &next;
    st.update(ctx, InputFrame{}, 16.0f);
    CHECK(st.contains(&next));
    st.push(&next);  // doppelt angefordert: bleibt eine Anforderung
    for (int i = 0; i < 40; ++i) st.update(ctx, InputFrame{}, 16.0f);
    CHECK(st.depth() == 2);
    CHECK(st.top() == &next);
    CHECK(next.enters == 1);
}

TEST_CASE_FIXTURE(Fixture, "eine ausblendende Szene lässt sich danach wieder öffnen") {
    SceneStack st;
    Recorder a, d(true);
    st.push(&a);
    st.push(&d);
    d.next.pop = true;
    st.update(ctx, InputFrame{}, 16.0f);
    CHECK_FALSE(st.contains(&d));  // geht gerade
    st.push(&d);
    for (int i = 0; i < 40; ++i) st.update(ctx, InputFrame{}, 16.0f);
    CHECK(st.depth() == 2);
    CHECK(st.top() == &d);
    CHECK(d.enters == 2);
}

TEST_CASE_FIXTURE(Fixture, "M29: Stapel trägt Einstellungen → Cloud → Kopplung → Tastatur über dem Hauptmenü") {
    SceneStack stack;
    Recorder s[6] = {Recorder(), Recorder(), Recorder(true), Recorder(true), Recorder(true), Recorder(true)};
    for (Recorder& r : s) stack.push(&r);
    CHECK(stack.depth() == 6);
    CHECK(stack.top() == &s[5]);
}
