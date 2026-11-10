#include "core/library_store.hpp"
#include "ui/details_scene.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include <vector>
#include "ui/strings.hpp"
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

TEST_CASE_FIXTURE(Fixture, "Details: A auf Starten liefert Launch mit dem Spiel (samt Pfad); Back schließt") {
    DetailsScene d;
    std::string path = "roms/nes/a.nes";
    Game g{Console::Nes, "nes/a", "A", 0, 0};
    g.path = path.c_str();
    d.open(g, true, 0);
    path = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";  // die Szene hält eine eigene Kopie
    const SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::Launch);
    REQUIRE(r.game != nullptr);
    CHECK(std::strcmp(r.game->key, "nes/a") == 0);
    REQUIRE(r.game->path != nullptr);
    CHECK(std::strcmp(r.game->path, "roms/nes/a.nes") == 0);
    CHECK_FALSE(r.pop);
    CHECK(step(d, action(Action::Back)).pop);
}

TEST_CASE_FIXTURE(Fixture, "Details: Zeiger neben den Knöpfen + A liefert nichts") {
    DetailsScene d;
    d.open(Game{Console::Nes, "nes/a", "A", 0, 0}, false, 0);
    InputFrame f;
    f.pointerValid = true;
    f.pointerMoved = true;
    f.pointerX = 5;
    f.pointerY = 5;
    step(d, f);
    f.pointerMoved = false;
    f.actions = 1u << static_cast<u32>(Action::Confirm);
    CHECK(step(d, f).command == AppCommand::None);
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

namespace {
const Game kSm64{Console::N64, "n64/sm64.z64", "Super Mario 64", 1996, 1};
N64Decision decision(bool vc, bool not64, bool wii64) {
    N64Inputs in;
    static const std::vector<u64> installed{0x000100014E414D50ULL};
    if (vc) {
        in.mappedTitle = 0x000100014E414D50ULL;
        in.installedTitles = installed;
    }
    in.not64Dol = not64;
    in.wii64Dol = wii64;
    return decideN64(in);
}
constexpr int choiceOf(N64Method m) { return 1 + static_cast<int>(m); }
}  // namespace

TEST_CASE_FIXTURE(Fixture, "N64: Hoch fokussiert die Startmethode, Runter zurück auf Starten") {
    DetailsScene d;
    d.open(kSm64, false, 0);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == 0);  // ohne setN64 keine Zeile
    d.setN64(decision(false, true, true), std::nullopt);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusMethod);
    step(d, action(Action::Down));
    CHECK(d.focusedButton() == 0);
    d.open(Game{Console::Nes, "nes/x.nes", "X", 0, 0}, false, 0);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == 0);
}

TEST_CASE_FIXTURE(Fixture, "N64: Rechts wechselt Automatisch → nächster verfügbarer Weg und meldet SetN64Method") {
    DetailsScene d;
    d.open(kSm64, false, 0);
    d.setN64(decision(false, true, true), std::nullopt);
    CHECK(d.methodChoice() == 0);
    step(d, action(Action::Up));
    SceneRequests r = step(d, action(Action::Right));
    CHECK(r.command == AppCommand::SetN64Method);
    CHECK(r.n64Choice == choiceOf(N64Method::Not64));
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->key) == "n64/sm64.z64");
    CHECK(d.methodChoice() == choiceOf(N64Method::Not64));
    r = step(d, action(Action::Right));
    CHECK(r.n64Choice == choiceOf(N64Method::Wii64));
    r = step(d, action(Action::Right));
    CHECK(r.n64Choice == 0);
    CHECK(d.methodChoice() == 0);
}

TEST_CASE_FIXTURE(Fixture, "N64: Links läuft rückwärts und überspringt Nicht-Verfügbares") {
    DetailsScene d;
    d.open(kSm64, false, 0);
    d.setN64(decision(false, true, true), std::nullopt);
    step(d, action(Action::Up));
    SceneRequests r = step(d, action(Action::Left));
    CHECK(r.n64Choice == choiceOf(N64Method::Wii64));
    step(d, action(Action::Left));
    r = step(d, action(Action::Left));
    CHECK(r.n64Choice == 0);  // Not64 → Automatisch, VC übersprungen
}

TEST_CASE_FIXTURE(Fixture, "N64: gesetzter, nicht verfügbarer Override bleibt sichtbar gewählt") {
    DetailsScene d;
    d.open(kSm64, false, 0);
    const N64Decision dec = decision(false, true, false);
    REQUIRE(dec.why[static_cast<std::size_t>(N64Method::Wii64)] == N64Unavailable::DolMissing);
    d.setN64(dec, N64Method::Wii64);
    CHECK(d.methodChoice() == choiceOf(N64Method::Wii64));
    step(d, action(Action::Up));
    const SceneRequests r = step(d, action(Action::Right));
    CHECK(r.n64Choice == 0);
}

TEST_CASE("Hinweiszeile nennt Gründe") {
    char buf[256];
    n64HintLine(decision(false, true, false), buf, sizeof buf);
    CHECK(std::string(buf) == "VC-Inject: nicht zugeordnet · Wii64: DOL fehlt");
    N64Inputs in;
    in.override_ = N64Method::Wii64;
    in.not64Dol = true;
    n64HintLine(decideN64(in), buf, sizeof buf);
    CHECK(std::string(buf) == std::string(kStrOverrideIgnored) + " VC-Inject: nicht zugeordnet · Wii64: DOL fehlt");
    n64HintLine(decision(true, true, true), buf, sizeof buf);
    CHECK(std::string(buf).empty());
}
