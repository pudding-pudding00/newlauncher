#include "core/library_store.hpp"
#include "ui/details_scene.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string>
using namespace nl;
using namespace nl::ui;

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
    template <typename S>
    SceneRequests step(S& s, const InputFrame& in, float dt = 16) {
        SceneRequests r;
        s.update(ctx, in, dt, r);
        return r;
    }
};
}  // namespace

TEST_CASE("detailsInfoLine mit allen Feldern, ohne Jahr, ohne Verlauf") {
    char buf[128];
    const Game g{Console::N64, "n64/sm64.z64", "Super Mario 64", 1996, 1};
    detailsInfoLine(g, 1762646400u, buf, sizeof buf);  // 2025-11-09 00:00 UTC
    CHECK(std::string(buf) == "N64 · Gen 5 · 1996 · 1 Spieler · zuletzt 09.11.2025");
    detailsInfoLine(Game{Console::Wii, "RMCP01", "MKW", 0, 4}, 0, buf, sizeof buf);
    CHECK(std::string(buf) == "Wii · Gen 7 · 4 Spieler");
    detailsInfoLine(Game{Console::Nes, "nes/x", "X", 0, 0}, 0, buf, sizeof buf);
    CHECK(std::string(buf) == "NES · Gen 3");
    detailsInfoLine(g, 951782400u, buf, sizeof buf);  // 2000-02-29 (Schaltjahr)
    CHECK(std::string(buf).find("zuletzt 29.02.2000") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Details: Left/Right zwischen Starten und Favorit, Confirm auf Favorit → ToggleFavorite") {
    DetailsScene d;
    const Game g{Console::N64, "n64/sm64.z64", "Super Mario 64", 1996, 1};
    d.open(g, false, 0);
    CHECK(d.focusedButton() == 0);
    step(d, action(Action::Left));
    CHECK(d.focusedButton() == 0);
    step(d, action(Action::Right));
    step(d, action(Action::Right));
    CHECK(d.focusedButton() == 1);
    const SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::ToggleFavorite);
    REQUIRE(r.game != nullptr);
    CHECK(std::strcmp(r.game->key, "n64/sm64.z64") == 0);
    CHECK(step(d, action(Action::Favorite)).command == AppCommand::ToggleFavorite);
}

TEST_CASE_FIXTURE(Fixture, "Details: Starten zeigt Hinweis, kein Kommando; Back schließt") {
    DetailsScene d;
    d.open(Game{Console::Nes, "nes/a", "A", 0, 0}, true, 0);
    const SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::None);
    CHECK_FALSE(r.pop);
    CHECK(std::string(d.hintText()) == "Starten folgt mit M3");
    step(d, InputFrame{}, 2100);
    CHECK(std::string(d.hintText()) != "Starten folgt mit M3");
    CHECK(step(d, action(Action::Back)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Details: hält eigene Kopien der Spieltexte") {
    DetailsScene d;
    std::string key = "nes/a", title = "Titel";
    d.open(Game{Console::Nes, key.c_str(), title.c_str(), 0, 0}, false, 0);
    key = "XXXXXXXXXXXXXXXXXXXXXXXX";
    title = "YYYYYYYYYYYYYYYYYYYYYYYY";
    REQUIRE(d.game() != nullptr);
    CHECK(std::strcmp(d.game()->key, "nes/a") == 0);
    CHECK(std::strcmp(d.game()->title, "Titel") == 0);
}

TEST_CASE_FIXTURE(Fixture, "Details: ohne Beschreibung und mit 500 Zeichen Beschreibung – keine verworfenen Quads, alles im Bild") {
    const std::string longDesc(500, 'w');
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        for (const char* desc : {static_cast<const char*>(nullptr), longDesc.c_str()}) {
            DetailsScene d;
            Game g{Console::Gc, "GM8P01", "Ein sehr langer Titel, der sicher umbrechen muss, weil er so lang ist", 2002, 1};
            g.description = desc;
            d.open(g, false, 1762646400u);
            dl.clear();
            d.draw(ctx, dl);
            CHECK(dl.droppedQuads() == 0);
            for (const gfx::Vertex& v : dl.vertices()) {
                CHECK(v.x >= -1);
                CHECK(v.x <= w + 1);
                CHECK(v.y <= ctx.height + 1);
            }
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Confirm auf fokussierter Kachel öffnet Details; ohne Fokus nicht") {
    const Library lib = testLibrary();
    MainScene s(lib);
    DetailsScene d;
    s.setDetails(&d, nullptr);
    step(s, InputFrame{});
    const SceneRequests r = step(s, action(Action::Confirm));
    CHECK(r.push == &d);
    REQUIRE(d.game() != nullptr);
    CHECK(std::string(d.game()->title) == std::string(s.infoTitle()));
    step(s, action(Action::NextConsole));  // Zuletzt: leer
    CHECK(step(s, action(Action::Confirm)).push == nullptr);
}
