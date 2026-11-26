#include "core/library_store.hpp"
#include "core/n64.hpp"
#include "core/parental.hpp"
#include "ui/home_menu.hpp"
#include "ui/main_scene.hpp"
#include "ui/strings.hpp"
#include "ui/test_library.hpp"
#include "support/fake_covers.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct DummyScene final : Scene {
    void update(const UiContext&, const InputFrame&, float, SceneRequests&) override {}
    void draw(const UiContext&, gfx::DrawList&) override {}
    bool isOverlay() const override { return true; }
};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{4096, 16};
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        ctx.minuteOfDay = 19 * 60 + 42;
    }
    static InputFrame action(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame pointerAction(float x, float y, Action a) {
        InputFrame f = pointer(x, y, false);
        f.actions = 1u << static_cast<u32>(a);
        return f;
    }
    static InputFrame pointer(float x, float y, bool moved = true) {
        InputFrame f;
        f.pointerValid = true;
        f.pointerMoved = moved;
        f.pointerX = x;
        f.pointerY = y;
        return f;
    }
    SceneRequests step(MainScene& s, const InputFrame& in, float dt = 16) {
        SceneRequests r;
        s.update(ctx, in, dt, r);
        return r;
    }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Start: Leiste auf dem ersten Eintrag mit Spielen, Fokus 0") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Right/Down bewegen den Fokus; NextConsole wechselt die Liste und setzt Fokus 0") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 2);  // NES
    step(s, action(Action::Right));
    step(s, action(Action::Down));
    CHECK(s.focusIndex() == 7);
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 3);
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "PrevConsole am Anfang und NextConsole am Ende bleiben stehen") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::PrevConsole));
    CHECK(s.barIndex() == 0);
    for (int i = 0; i < 20; ++i) step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 8);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger bewegt → Fokus auf der Kachel unter dem Zeiger") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));  // N64, 20 Spiele
    const gfx::Rect r = s.grid().tileRect(2, 0);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2));
    CHECK(s.focusIndex() == 2);
}

TEST_CASE_FIXTURE(Fixture, "ruhender Zeiger überschreibt den Steuerkreuz-Fokus nicht") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
    const gfx::Rect r = s.grid().tileRect(2, 0);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2));
    InputFrame f = pointer(r.x + r.w / 2, r.y + r.h / 2, false);
    f.actions = 1u << static_cast<u32>(Action::Right);
    step(s, f);
    CHECK(s.focusIndex() == 3);
    step(s, pointer(r.x + r.w / 2, r.y + r.h / 2, false));
    CHECK(s.focusIndex() == 3);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger bewegt auf leere Fläche → kein Fokus, Infoleiste zeigt Datum") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect r = s.grid().tileRect(0, 0);
    step(s, pointer(r.x + 10, r.y + 10));  // erst auf eine Kachel zeigen …
    step(s, pointer(r.x - 6, r.y + 10));   // … dann in die Lücke
    CHECK(s.focusIndex() == -1);
    CHECK(std::string(s.infoTitle()).find("November") == std::string::npos);  // Standarddatum: Januar
    CHECK(std::string(s.infoTitle()).find("2026") != std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger erscheint neben den Kacheln: Startfokus bleibt erhalten") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect r = s.grid().tileRect(5, 0);
    step(s, pointer(r.x + r.w + 40, r.y + 10));
    CHECK(s.focusIndex() == 0);
}

TEST_CASE_FIXTURE(Fixture, "leerer Eintrag (Zuletzt): kein Fokus, Richtungen und Confirm ohne Wirkung") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 1);
    CHECK(s.focusIndex() == -1);
    step(s, action(Action::Right));
    step(s, action(Action::Down));
    SceneRequests r = step(s, action(Action::Confirm));
    CHECK(s.focusIndex() == -1);
    CHECK(r.push == nullptr);
    CHECK(r.command == AppCommand::None);
    s.draw(ctx, dl);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "leere Bibliothek: draw und update laufen, Text wird gezeichnet") {
    MainScene s(Library{});
    step(s, action(Action::Down));
    step(s, action(Action::NextConsole));
    s.draw(ctx, dl);
    CHECK(dl.quadCount() > 0);
    CHECK(s.focusIndex() == -1);
}

TEST_CASE_FIXTURE(Fixture, "Confirm auf dem Zahnrad liefert CycleTheme und zeigt die Darstellung an") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect g = s.gearRect(ctx);
    step(s, pointer(g.x + g.w / 2, g.y + g.h / 2));
    SceneRequests r = step(s, pointerAction(g.x + g.w / 2, g.y + g.h / 2, Action::Confirm));
    CHECK(r.command == AppCommand::CycleTheme);
    ctx.themeMode = ThemeMode::Light;
    step(s, InputFrame{});
    CHECK(std::string(s.infoLineText()).find("Darstellung: Hell") != std::string::npos);
    step(s, InputFrame{}, 2100);
    CHECK(std::string(s.infoLineText()).find("Darstellung") == std::string::npos);
}

TEST_CASE_FIXTURE(Fixture, "ruhender Zeiger auf dem Zahnrad wirkt auch nach einem Frame ohne Eingaben") {
    const Library lib = testLibrary();
    MainScene s(lib);
    const gfx::Rect g = s.gearRect(ctx);
    step(s, pointer(g.x + g.w / 2, g.y + g.h / 2));
    step(s, InputFrame{});  // z. B. HOME-Menü lag darüber
    CHECK(step(s, pointerAction(g.x + g.w / 2, g.y + g.h / 2, Action::Confirm)).command == AppCommand::CycleTheme);
}

TEST_CASE_FIXTURE(Fixture, "Konsolenleiste nutzt die ganze Breite mit gleichen Abständen") {
    const Library lib = testLibrary();
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        MainScene s(lib);
        step(s, InputFrame{});
        const GridMetrics m = gridMetrics(w);
        CHECK(s.barRect(0).x == doctest::Approx(m.marginX));
        const gfx::Rect last = s.barRect(8);
        CHECK(last.x + last.w == doctest::Approx(w - m.marginX));
        const float gap0 = s.barRect(1).x - (s.barRect(0).x + s.barRect(0).w);
        const float gap5 = s.barRect(6).x - (s.barRect(5).x + s.barRect(5).w);
        CHECK(gap0 == doctest::Approx(gap5));
        CHECK(gap0 > 6.0f);
    }
}

TEST_CASE_FIXTURE(Fixture, "Home fordert push des HOME-Menüs an") {
    const Library lib = testLibrary();
    MainScene s(lib);
    DummyScene home;
    s.setHomeMenu(&home);
    CHECK(step(s, action(Action::Home)).push == &home);
    const gfx::Rect h = s.homeRect(ctx);
    step(s, pointer(h.x + h.w / 2, h.y + h.h / 2));
    CHECK(step(s, pointerAction(h.x + h.w / 2, h.y + h.h / 2, Action::Confirm)).push == &home);
}

TEST_CASE_FIXTURE(Fixture, "M25: Zeiger auf dem WLAN-Symbol + Confirm öffnet die WLAN-Info") {
    const Library lib = testLibrary();
    MainScene s(lib);
    DummyScene wifi;
    step(s, InputFrame{});
    const gfx::Rect w = s.wifiRect(ctx);
    // ohne gesetzte Szene: kein Hover, A wirkt aufs Raster
    CHECK(step(s, pointerAction(w.x + w.w / 2, w.y + w.h / 2, Action::Confirm)).push != &wifi);
    s.setWifiScene(&wifi);
    step(s, pointer(w.x + w.w / 2, w.y + w.h / 2));
    CHECK(step(s, pointerAction(w.x + w.w / 2, w.y + w.h / 2, Action::Confirm)).push == &wifi);
}

TEST_CASE_FIXTURE(Fixture, "M25: WLAN-Symbol zeichnet alle Zustände") {
    const Library lib = testLibrary();
    MainScene s(lib);
    for (WifiStatus st : {WifiStatus::Connecting, WifiStatus::Connected, WifiStatus::Limited,
                          WifiStatus::Disconnected}) {
        ctx.wifi = st;
        for (int i = 0; i < 20; ++i) step(s, InputFrame{}, 100);
        gfx::DrawList dl{65536, 16};
        s.draw(ctx, dl);
        CHECK(dl.quadCount() > 0);
    }
}

TEST_CASE_FIXTURE(Fixture, "Zeiger über einem Leisten-Eintrag + Confirm wählt ihn aus") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    const gfx::Rect b = s.barRect(4);
    step(s, pointer(b.x + b.w / 2, b.y + b.h / 2));
    step(s, pointerAction(b.x + b.w / 2, b.y + b.h / 2, Action::Confirm));
    CHECK(s.barIndex() == 4);
}

TEST_CASE_FIXTURE(Fixture, "draw bleibt innerhalb der DrawList-Kapazität") {
    const Library lib = testLibrary();
    for (float w : {640.0f, 854.0f}) {
        ctx.width = w;
        MainScene s(lib);
        for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
        step(s, InputFrame{}, 300);
        dl.clear();
        s.draw(ctx, dl);
        CHECK(dl.droppedQuads() == 0);
        CHECK(dl.quadCount() < 3000);
        for (const gfx::Vertex& v : dl.vertices()) {
            CHECK(v.x >= -40);
            CHECK(v.x <= w + 40);
        }
    }
}

TEST_CASE_FIXTURE(Fixture, "200 Zeichen langer Titel mit ungültigem UTF-8: Quads bleiben in der Kachel") {
    std::string title(200, 'W');
    title[50] = '\xC3';
    title[120] = '\xFF';
    const Game games[] = {Game{Console::Nes, "x", title.c_str(), 1990, 1}};
    Library lib;
    lib.games = games;
    MainScene s(lib);
    step(s, action(Action::NextConsole));
    step(s, action(Action::NextConsole));
    CHECK(s.barIndex() == 2);
    step(s, action(Action::Left));  // Fokus weg von Kachel 0 geht nicht; Fokus bleibt
    step(s, InputFrame{}, 300);
    // Nur das Raster zeichnen lassen und prüfen, dass der Titeltext die Kachelbreite einhält.
    dl.clear();
    s.drawTile(ctx, dl, 0, s.grid().tileRect(0, 0), false);
    const gfx::Rect r = s.grid().tileRect(0, 0);
    for (const gfx::Vertex& v : dl.vertices()) {
        CHECK(v.x >= r.x - 0.01f);
        CHECK(v.x <= r.x + r.w + 0.01f);
        CHECK(v.y <= r.y + r.h + 0.01f);
    }
}

namespace {
const Game kLibA[] = {
    Game{Console::Nes, "nes/a", "Alpha", 0, 0},  Game{Console::Nes, "nes/b", "Beta", 0, 0},
    Game{Console::Nes, "nes/c", "Gamma", 0, 0},  Game{Console::N64, "n64/x", "Xeno", 0, 0},
    Game{Console::Wii, "RMCP01", "Mario Kart Wii", 0, 0},
};
// Neue Liste: Spiel "nes/a" entfernt, "nes/d" neu, N64 verschwunden.
const Game kLibB[] = {
    Game{Console::Nes, "nes/b", "Beta", 0, 0},
    Game{Console::Nes, "nes/c", "Gamma", 0, 0},
    Game{Console::Nes, "nes/d", "Delta", 0, 0},
    Game{Console::Wii, "RMCP01", "Mario Kart Wii", 0, 0},
};
Library libOf(std::span<const Game> games) { return Library{games, {}, {}}; }
}  // namespace

TEST_CASE_FIXTURE(Fixture, "setLibrary behält Leisten-Eintrag und fokussiertes Spiel per Schlüssel") {
    MainScene s(libOf(kLibA));
    // Leiste: Favoriten, Zuletzt, Wii, N64, NES → NES ist Index 4
    for (int i = 0; i < 4; ++i) step(s, action(Action::NextConsole));
    REQUIRE(s.barIndex() == 4);
    step(s, action(Action::Right));
    step(s, action(Action::Right));  // Fokus auf "Gamma" (Index 2)
    REQUIRE(s.focusIndex() == 2);
    s.setLibrary(libOf(kLibB), s.focusedKey());
    step(s, InputFrame{});
    CHECK(s.barIndex() == 3);  // N64 weg: NES rückt auf Index 3
    CHECK(s.focusIndex() == 1);  // "Gamma" steht in der neuen NES-Liste an Position 1
    CHECK(std::string(s.infoTitle()) == "Gamma");
}

TEST_CASE_FIXTURE(Fixture, "fokussiertes Spiel entfernt → Fokus 0") {
    MainScene s(libOf(kLibA));
    for (int i = 0; i < 4; ++i) step(s, action(Action::NextConsole));
    REQUIRE(std::string(s.infoTitle()) == "Alpha");
    s.setLibrary(libOf(kLibB), s.focusedKey());
    step(s, InputFrame{});
    CHECK(s.focusIndex() == 0);
    CHECK(std::string(s.infoTitle()) == "Beta");
}

TEST_CASE_FIXTURE(Fixture, "Konsole verschwindet → Leiste fällt auf den Eintrag an gleicher Position bzw. den letzten") {
    MainScene s(libOf(kLibA));
    step(s, action(Action::NextConsole));  // Start bei Wii (erster Eintrag mit Spielen) → N64
    REQUIRE(s.barIndex() == 3);
    s.setLibrary(libOf(kLibB), s.focusedKey());
    step(s, InputFrame{});
    CHECK(s.barIndex() == 3);  // jetzt NES an derselben Position
    CHECK(s.focusIndex() == 0);
    for (int i = 0; i < 5; ++i) step(s, action(Action::NextConsole));
    s.setLibrary(libOf(std::span<const Game>(kLibB, 1)), s.focusedKey());  // nur noch NES
    step(s, InputFrame{});
    CHECK(s.barIndex() == 2);  // letzter Eintrag
}

TEST_CASE_FIXTURE(Fixture, "setLibrary mit leerer Bibliothek → nur Favoriten/Zuletzt, kein Absturz") {
    MainScene s(libOf(kLibA));
    for (int i = 0; i < 4; ++i) step(s, action(Action::NextConsole));
    s.setLibrary(Library{}, s.focusedKey());
    step(s, action(Action::Right));
    s.draw(ctx, dl);
    CHECK(s.barIndex() <= 1);
    CHECK(s.focusIndex() == -1);
    CHECK(dl.droppedQuads() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Favorit-Taste mit Fokus liefert ToggleFavorite mit dem Spiel; ohne Fokus nichts") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    const SceneRequests r = step(s, action(Action::Favorite));
    CHECK(r.command == AppCommand::ToggleFavorite);
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->title) == std::string(s.infoTitle()));
    step(s, action(Action::NextConsole));  // Zuletzt: leer, kein Fokus
    const SceneRequests none = step(s, action(Action::Favorite));
    CHECK(none.command == AppCommand::None);
    CHECK(none.game == nullptr);
    CHECK(std::string(appCommandName(AppCommand::ToggleFavorite)) == "toggle_favorite");
}

TEST_CASE_FIXTURE(Fixture, "showToast ersetzt die Infozeile für 2 s") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    s.showToast("Zu Favoriten hinzugefügt");
    step(s, InputFrame{}, 100);
    CHECK(std::string(s.infoLineText()) == "Zu Favoriten hinzugefügt");
    step(s, InputFrame{}, 2000);
    CHECK(std::string(s.infoLineText()) != "Zu Favoriten hinzugefügt");
}

TEST_CASE_FIXTURE(Fixture, "Favoriten-Kachel trägt eine Markierung (ein Quad mehr)") {
    IndexData d;
    d.games.push_back(IndexGame{Console::Nes, 0, "nes/a.nes", "Gleich", "roms/nes/a.nes", 0});
    d.games.push_back(IndexGame{Console::Nes, 0, "nes/b.nes", "Gleich", "roms/nes/b.nes", 0});
    LibraryStore store;
    store.setGames(std::move(d), nullptr);
    store.toggleFavorite("nes/a.nes");
    MainScene s(Library{store.games(), store.favorites(), store.recent()});
    s.setFavoriteKeys(&store);
    for (int i = 0; i < 3; ++i) step(s, action(Action::NextConsole));  // NES-Liste
    REQUIRE(s.grid().count() == 2);
    const gfx::Rect r = s.grid().tileRect(0, 0);
    dl.clear();
    s.drawTile(ctx, dl, 0, r, false);
    const u32 fav = dl.quadCount();
    dl.clear();
    s.drawTile(ctx, dl, 1, r, false);
    CHECK(fav == dl.quadCount() + 1);
}

namespace {
IndexData nesIndex(std::initializer_list<const char*> titles) {
    IndexData d;
    for (const char* t : titles)
        d.games.push_back(IndexGame{Console::Nes, 0, std::string("nes/") + t, t, std::string("roms/nes/") + t, 0});
    return d;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Bibliothekswechsel über LibraryStore: Fokus bleibt per Schlüssel trotz neuer Reihenfolge und Größe") {
    LibraryStore store;
    store.setGames(nesIndex({"Aardvark", "Alpha", "Beta", "Gamma"}), nullptr);
    MainScene s(Library{store.games(), store.favorites(), store.recent()});
    for (int i = 0; i < 2; ++i) step(s, action(Action::NextConsole));  // Favoriten, Zuletzt, NES
    REQUIRE(s.barIndex() == 2);
    for (int i = 0; i < 3; ++i) step(s, action(Action::Right));
    REQUIRE(std::string(s.infoTitle()) == "Gamma");
    const std::string key = s.focusedKey();  // vor jeder Änderung am Speicher holen
    store.setGames(nesIndex({"Alpha", "Beta", "Gamma", "Zeta", "Omega", "Delta"}), nullptr);
    s.setLibrary(Library{store.games(), store.favorites(), store.recent()}, key);
    step(s, InputFrame{});
    CHECK(std::string(s.infoTitle()) == "Gamma");
}

TEST_CASE_FIXTURE(Fixture, "Favorit im Favoriten-Eintrag entfernen: kein Zugriff auf die alte Liste") {
    LibraryStore store;
    store.setGames(nesIndex({"A", "B", "C"}), nullptr);
    store.toggleFavorite("nes/A");
    store.toggleFavorite("nes/B");
    store.toggleFavorite("nes/C");
    MainScene s(Library{store.games(), store.favorites(), store.recent()});
    step(s, action(Action::Right));
    step(s, action(Action::Right));
    REQUIRE(std::string(s.infoTitle()) == "C");
    const std::string key = s.focusedKey();
    store.toggleFavorite("nes/A");
    s.setLibrary(Library{store.games(), store.favorites(), store.recent()}, key);
    step(s, InputFrame{});
    CHECK(s.barIndex() == 0);
    CHECK(std::string(s.infoTitle()) == "C");
    CHECK(s.focusIndex() == 1);
}

namespace {
struct ManyGames {
    std::vector<std::string> keys, titles;
    std::vector<Game> games;
    explicit ManyGames(int n) {
        for (int i = 0; i < n; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%02d", i);
            keys.push_back(std::string("nes/") + buf);
            titles.push_back(std::string("Spiel ") + buf);
        }
        for (int i = 0; i < n; ++i) games.push_back(Game{Console::Nes, keys[i].c_str(), titles[i].c_str(), 0, 0});
    }
    Library library() const { return Library{games, {}, {}}; }
};

bool hasCoverBatch(const gfx::DrawList& dl, gfx::TextureId tex) {
    for (const gfx::Batch& b : dl.batches())
        if (b.texture == tex) return true;
    return false;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Cover: sichtbare Kacheln Visible, zwei Reihen darunter Prefetch, Rest gar nicht") {
    const ManyGames many(60);
    MainScene s(many.library());
    test::FakeCovers covers;
    s.setCovers(&covers);
    step(s, InputFrame{});
    const GridModel& g = s.grid();
    const float top = g.viewTop(), bottom = top + g.viewHeight();
    int visible = 0, prefetch = 0;
    for (int i = 0; i < g.count(); ++i) {
        const gfx::Rect r = g.tileRect(i, 0.0f);
        const auto& c = covers.wants[many.keys[i]];
        CAPTURE(i);
        if (r.y + r.h >= top && r.y <= bottom) {
            CHECK(c.visible == 1);
            CHECK(c.prefetch == 0);
            ++visible;
        } else if (r.y <= bottom + 2 * g.rowPitch()) {
            CHECK(c.prefetch == 1);
            CHECK(c.visible == 0);
            ++prefetch;
        } else {
            CHECK(c.visible + c.prefetch == 0);
        }
    }
    CHECK(visible > 0);
    CHECK(prefetch > 0);
    CHECK(covers.wants["nes/59"].visible + covers.wants["nes/59"].prefetch == 0);
}

TEST_CASE_FIXTURE(Fixture, "Cover: Quad mit Textur und Alpha aus dem Provider") {
    const ManyGames many(3);
    MainScene s(many.library());
    test::FakeCovers covers;
    covers.views["nes/00"] = CoverView{7, 0.5f};
    s.setCovers(&covers);
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    const gfx::Batch* cover = nullptr;
    for (const gfx::Batch& b : dl.batches())
        if (b.texture == 7) cover = &b;
    REQUIRE(cover != nullptr);
    CHECK(cover->vertexCount == 4);
    const gfx::Vertex& v = dl.vertices()[cover->firstVertex];
    CHECK((v.color.a == 127 || v.color.a == 128));
    CHECK(v.u == 0.0f);
    CHECK(v.v == 0.0f);
    // Seitenverhältnis 5:7
    const gfx::Vertex& br = dl.vertices()[cover->firstVertex + 2];
    CHECK((br.x - v.x) / (br.y - v.y) == doctest::Approx(5.0 / 7.0).epsilon(0.01));
}

TEST_CASE_FIXTURE(Fixture, "Cover: ohne Textur bleibt der Platzhalter, ohne Provider auch") {
    const ManyGames many(3);
    MainScene s(many.library());
    test::FakeCovers covers;
    s.setCovers(&covers);
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    for (const gfx::Batch& b : dl.batches()) CHECK(b.texture == 0);
    s.setCovers(nullptr);
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    CHECK_FALSE(hasCoverBatch(dl, 7));
}

TEST_CASE_FIXTURE(Fixture, "Klänge: rechts → Move, ohne Wirkung still, HOME → Open") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    CHECK(step(s, action(Action::Right)).sound == UiSound::Move);
    CHECK(step(s, action(Action::Up)).sound == UiSound::None);  // oberste Zeile: kein Fokuswechsel
    CHECK(step(s, InputFrame{}).sound == UiSound::None);
    HomeMenu home;
    s.setHomeMenu(&home);
    CHECK(step(s, action(Action::Home)).sound == UiSound::Open);
}

TEST_CASE_FIXTURE(Fixture, "Hintergrundbild: ein Quad mit kBackgroundTexture über den ganzen Bildschirm, ohne Bild keiner") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    auto countBg = [&]() {
        dl.clear();
        s.draw(ctx, dl);
        int n = 0;
        for (const gfx::Batch& b : dl.batches())
            if (b.texture == gfx::kBackgroundTexture) n += static_cast<int>(b.vertexCount);
        return n;
    };
    CHECK(countBg() == 0);
    ctx.background = true;
    ctx.backgroundU1 = 0.5f;
    ctx.backgroundV1 = 1.0f;
    CHECK(countBg() == 4);
    const gfx::Batch& first = dl.batches()[1];  // direkt nach der Grundfläche
    CHECK(first.texture == gfx::kBackgroundTexture);
    CHECK(dl.vertices()[first.firstVertex + 2].u == doctest::Approx(0.5f));
}

// --- M17: Filter je Leiste ---
namespace {
struct FilterLib {
    Game games[4] = {
        Game{Console::Nes, "nes/a", "Alpha", 0, 0},
        Game{Console::Nes, "nes/b", "Beta", 0, 0},
        Game{Console::Nes, "nes/c", "Gamma", 0, 0},
        Game{Console::Snes, "snes/d", "Delta", 0, 0},
    };
    FilterLib() { games[1].genres = 1u << 3; }
    Library lib() const { return Library{games, {}, {}}; }
};
int barOf(const MainScene& m, const char* label) {
    for (int i = 0; i < 16; ++i)
        if (std::string(m.barLabelText(i)) == label) return i;
    return -1;
}
}  // namespace

TEST_CASE("MainScene: Kategorie-Filter zeigt nur deren Spiele; Konsolenwechsel setzt auf Alle zurück") {
    FilterLib fl;
    MainScene m(fl.lib());
    Categories cats = Categories::fromText("[Party]\nnes/c\nnes/a\n");
    m.setCategories(&cats);
    const int nes = barOf(m, "NES"), snes = barOf(m, "SNES");
    REQUIRE(nes >= 0);
    REQUIRE(snes >= 0);
    m.selectBarForTest(nes);
    REQUIRE(m.filterCount() == 4);  // Alle, Favoriten, Sport, Party
    CHECK(m.filterLabel(3) == "Party");
    m.setFilterIndex(3);
    CHECK(m.grid().count() == 2);
    m.setFocusForTest(1);
    CHECK(m.focusedKey() == "nes/c");
    cats.set("Party", "nes/b", true);
    m.refreshFilter();
    CHECK(m.grid().count() == 3);
    CHECK(m.focusedKey() == "nes/c");  // Fokus bleibt auf dem Spiel
    m.selectBarForTest(snes);
    CHECK(m.filterIndex() == 0);
    CHECK(m.grid().count() == 1);
}

TEST_CASE("MainScene: die Suche übernimmt den Filter des Eintrags davor (Filterzeile im Such-Overlay)") {
    FilterLib fl;
    MainScene m(fl.lib());
    const Categories cats = Categories::fromText("[Party]\nnes/a\nsnes/d\n");
    m.setCategories(&cats);
    m.selectBarForTest(barOf(m, "NES"));
    REQUIRE(m.filterLabel(3) == "Party");
    m.setFilterIndex(3);
    m.setQuery("a");  // Alpha, Beta, Gamma, Delta; Party davon: Alpha, Delta
    CHECK(m.filterLabel(m.filterIndex()) == "Party");
    CHECK(m.grid().count() == 2);
    m.setQuery("al");
    CHECK(m.grid().count() == 1);  // gleicher Eintrag: Filter bleibt
    CHECK(m.filterLabel(m.filterIndex()) == "Party");
    m.setQuery("");  // Text gelöscht, Tastatur noch offen: Filter bleibt auf dem Eintrag davor
    CHECK(m.filterLabel(m.filterIndex()) == "Party");
    CHECK(m.grid().count() == 1);  // NES-Party: nur Alpha
}

TEST_CASE("MainScene: ein Genre ohne Treffer bleibt als Filter sichtbar statt „Alle“ anzuzeigen") {
    FilterLib fl;
    MainScene m(fl.lib());
    m.selectBarForTest(barOf(m, "NES"));
    const std::string sport = m.filterLabel(2);  // Genre von Beta
    m.setFilterIndex(2);
    m.setQuery("gamma");
    CHECK(m.grid().count() == 0);
    CHECK(m.filterLabel(m.filterIndex()) == sport);
}

TEST_CASE("MainScene::setFilter liefert die Trefferzahl; unbekannte Kategorie → 0 Spiele") {
    FilterLib fl;
    MainScene m(fl.lib());
    const Categories cats = Categories::fromText("[Party]\nnes/a\n");
    m.setCategories(&cats);
    m.selectBarForTest(barOf(m, "NES"));
    CHECK(m.setFilter(*parseFilter("genre:sports")) == 1);
    CHECK(m.focusedKey() == "nes/b");
    CHECK(m.setFilter(*parseFilter("category:Fehlt")) == 0);
    CHECK(m.setFilter(GameFilter{}) == 3);
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Details öffnen setzt die Kategorien mit Häkchen fürs Spiel") {
    FilterLib fl;
    MainScene m(fl.lib());
    DetailsScene d;
    m.setDetails(&d, nullptr);
    const Categories cats = Categories::fromText("[Party]\nnes/b\n[Retro]\nnes/a\n");
    m.setCategories(&cats);
    m.selectBarForTest(barOf(m, "NES"));
    m.setFocusForTest(0);  // Alpha = nes/a
    const SceneRequests r = step(m, action(Action::Confirm));
    REQUIRE(r.push == &d);
    CHECK_FALSE(d.categoryChecked(0));
    CHECK(d.categoryChecked(1));
    SceneRequests out;
    d.update(ctx, action(Action::Up), 16, out);
    CHECK(d.focusedButton() == DetailsScene::kFocusCategories);
}

TEST_CASE("MainScene::setSort sortiert Konsoleneinträge sofort; Favoriten bleiben in ihrer Reihenfolge") {
    FilterLib fl;
    MainScene m(fl.lib());
    PlayStats stats;
    stats.record("nes/c", 10);
    stats.record("nes/c", 20);
    stats.record("nes/b", 30);
    const int nes = barOf(m, "NES");
    REQUIRE(nes >= 0);
    m.selectBarForTest(nes);
    m.setFocusForTest(0);
    CHECK(m.focusedKey() == "nes/a");
    m.setSort(SortOrder::Most, &stats);
    m.setFocusForTest(0);
    CHECK(m.focusedKey() == "nes/c");
    m.setFocusForTest(2);
    CHECK(m.focusedKey() == "nes/a");
    m.setSort(SortOrder::Recent, &stats);
    m.setFocusForTest(0);
    CHECK(m.focusedKey() == "nes/b");
    m.setSort(SortOrder::Title, nullptr);
    m.setFocusForTest(0);
    CHECK(m.focusedKey() == "nes/a");
}

TEST_CASE_FIXTURE(Fixture, "MainScene: Details öffnen zeigt die Statistik des Spiels (M17)") {
    FilterLib fl;
    MainScene m(fl.lib());
    DetailsScene d;
    m.setDetails(&d, nullptr);
    PlayStats stats;
    stats.record("nes/a", 1762128000u);
    stats.record("nes/a", 1762128000u);
    m.setSort(SortOrder::Title, &stats);
    m.selectBarForTest(barOf(m, "NES"));
    m.setFocusForTest(0);  // Alpha = nes/a
    REQUIRE(step(m, action(Action::Confirm)).push == &d);
    CHECK(std::string(d.statsText()) == "2-mal gestartet, zuletzt am 03.11.2025");
}

TEST_CASE_FIXTURE(Fixture, "MainScene: gesperrte Spiele tragen ein Schloss (M17)") {
    FilterLib fl;
    fl.games[2].rating = Rating{RatingSystem::Pegi, 16};
    MainScene m(fl.lib());
    m.selectBarForTest(barOf(m, "NES"));
    m.setFocusForTest(0);
    CHECK_FALSE(m.isGameLocked(fl.games[1]));  // ohne Kindersicherung nie
    dl.clear();
    m.draw(ctx, dl);
    const u32 unlockedQuads = dl.quadCount();

    constexpr std::array<u8, 16> salt{};
    ParentalConfig p = withPin(ParentalConfig{}, "1234", salt);
    p.maxAge = 12;
    const GameOverrides o = GameOverrides::fromIni(IniDoc::parse("[game nes/b]\nlocked = 1\n"));
    m.setParental(&p, &o);
    CHECK_FALSE(m.isGameLocked(fl.games[0]));
    CHECK(m.isGameLocked(fl.games[1]));  // locked = 1
    CHECK(m.isGameLocked(fl.games[2]));  // PEGI 16 > 12
    dl.clear();
    m.draw(ctx, dl);
    CHECK(dl.quadCount() > unlockedQuads);
    m.setParental(nullptr, nullptr);
    CHECK_FALSE(m.isGameLocked(fl.games[1]));
}

TEST_CASE_FIXTURE(Fixture, "Infoleiste zeigt WLAN-Anzeige links neben der Uhr ohne Überschneidung") {
    const Library lib = testLibrary();
    for (float width : {640.0f, 854.0f}) {
        ctx.width = width;
        MainScene s(lib);
        step(s, InputFrame{});
        const gfx::Rect w = s.wifiRect(ctx);
        const gfx::Rect h = s.homeRect(ctx);
        const gfx::Rect g = s.gearRect(ctx);
        CHECK(w.y >= ctx.height - 72.0f);
        CHECK(w.y + w.h <= ctx.height);
        CHECK(w.x > h.x + h.w);
        CHECK(w.x + w.w < g.x);
    }
}

TEST_CASE_FIXTURE(Fixture, "WLAN-Zustände (Disconnected, Connecting, Connected) zeichnen ohne Fehler in DE und EN") {
    const Library lib = testLibrary();
    MainScene s(lib);
    step(s, InputFrame{});
    const Lang savedLang = language();

    setLanguage(Lang::De);
    CHECK(std::string(tr(Str::Wifi)) == "WLAN");
    setLanguage(Lang::En);
    CHECK(std::string(tr(Str::Wifi)) == "Wi-Fi");

    u32 connectedQuads = 0;
    u32 disconnectedQuads = 0;
    for (Lang lang : {Lang::De, Lang::En}) {
        setLanguage(lang);
        for (WifiStatus st : {WifiStatus::Disconnected, WifiStatus::Connecting, WifiStatus::Connected}) {
            ctx.wifi = st;
            dl.clear();
            s.draw(ctx, dl);
            CHECK(dl.droppedQuads() == 0);
            if (lang == Lang::De) {
                if (st == WifiStatus::Connected) connectedQuads = dl.quadCount();
                if (st == WifiStatus::Disconnected) disconnectedQuads = dl.quadCount();
            }
        }
    }
    CHECK(disconnectedQuads > connectedQuads);  // roter Punkt bei Disconnected erzeugt zusätzliche Quads
    setLanguage(savedLang);
}

namespace {
struct MixedGames {  // je drei N64-, GB- und NES-Spiele
    std::vector<std::string> keys;
    std::vector<Game> games;
    MixedGames() {
        const std::pair<Console, const char*> kinds[] = {{Console::Nes, "nes/"}, {Console::Gb, "gb/"}, {Console::N64, "n64/"}};
        for (const auto& [c, prefix] : kinds)
            for (int i = 0; i < 3; ++i) keys.push_back(std::string(prefix) + char('a' + i));
        for (std::size_t i = 0; i < keys.size(); ++i)
            games.push_back(Game{i < 3 ? Console::Nes : i < 6 ? Console::Gb : Console::N64, keys[i].c_str(), keys[i].c_str(), 0, 0});
    }
    Library library() const { return Library{games, {}, {}}; }
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Kachelhöhe je Konsole nach Cover-Format (M23, Maße M24)") {
    ctx.width = 854;
    const MixedGames mixed;
    MainScene s(mixed.library());
    step(s, InputFrame{});
    int seen = 0;
    for (int i = 0; i < 5; ++i) {
        s.selectBarForTest(i);
        const std::string label = s.barLabelText(i);
        const float h = s.grid().tileRect(0, 0).h;
        float want = 179.0f;  // NES; leere Favoriten/Zuletzt: Grundmaß
        if (label == "N64") want = 90.0f;
        if (label == "GB") want = 128.0f;
        if (label == "N64" || label == "GB" || label == "NES") ++seen;
        CHECK(h == want);
        CHECK(s.grid().tileRect(0, 0).w == 128.0f);
    }
    CHECK(seen == 3);
}

TEST_CASE_FIXTURE(Fixture, "Cover: Inhaltsbereich als UV, eingepasst ohne Verzerrung (M23)") {
    ctx.width = 854;
    const MixedGames mixed;
    MainScene s(mixed.library());
    test::FakeCovers covers;
    CoverView cv{7, 1.0f};
    cv.uv = gfx::UvRect{0, 64.0f / 280, 1, 216.0f / 280};
    cv.aspect = 200.0f / 152.0f;
    covers.views["n64/a"] = cv;
    s.setCovers(&covers);
    step(s, InputFrame{});
    for (int i = 0; i < 5; ++i)
        if (std::string(s.barLabelText(i)) == "N64") s.selectBarForTest(i);
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    const gfx::Batch* cover = nullptr;
    for (const gfx::Batch& b : dl.batches())
        if (b.texture == 7) cover = &b;
    REQUIRE(cover != nullptr);
    const gfx::Vertex& tl = dl.vertices()[cover->firstVertex];
    const gfx::Vertex& br = dl.vertices()[cover->firstVertex + 2];
    CHECK(tl.v == doctest::Approx(64.0f / 280));
    CHECK(br.v == doctest::Approx(216.0f / 280));
    CHECK((br.x - tl.x) / (br.y - tl.y) == doctest::Approx(200.0 / 152.0).epsilon(0.01));
    CHECK(br.y - tl.y < 90.0f);  // passt in die flache N64-Kachel (fokussiert leicht vergrößert)
}

TEST_CASE_FIXTURE(Fixture, "Zeiger im unteren Randstreifen scrollt, Fokus folgt der Kachel darunter (M23)") {
    ctx.width = 854;
    const ManyGames many(60);  // 10 Reihen
    MainScene s(many.library());
    step(s, InputFrame{});
    const float bottom = s.grid().viewTop() + s.grid().viewHeight();
    const float x = s.grid().tileRect(2, 0).x + 20;
    step(s, pointer(x, bottom - 4));
    for (int i = 0; i < 15; ++i) step(s, pointer(x, bottom - 4, false), 16);  // 0,25 s
    const float after = s.grid().targetScroll();
    CHECK(after > 0.0f);
    CHECK(after <= s.grid().rowPitch() + 1.0f);  // höchstens etwa eine Reihe je 0,25 s
    CHECK(s.focusIndex() == s.grid().hitTest(x, bottom - 4, after));
    // Mitte: steht
    step(s, pointer(x, s.grid().viewTop() + s.grid().viewHeight() / 2));
    const float mid = s.grid().targetScroll();
    for (int i = 0; i < 30; ++i) step(s, pointer(x, s.grid().viewTop() + s.grid().viewHeight() / 2, false), 16);
    CHECK(s.grid().targetScroll() == mid);
    // Oberer Streifen: zurück, nie unter 0
    for (int i = 0; i < 300; ++i) step(s, pointer(x, s.grid().viewTop() + 2, false), 16);
    CHECK(s.grid().targetScroll() == 0.0f);
}

TEST_CASE_FIXTURE(Fixture, "Zeiger-Randscrollen endet an der letzten Reihe (M23)") {
    ctx.width = 854;
    const ManyGames many(60);
    MainScene s(many.library());
    step(s, InputFrame{});
    const float bottom = s.grid().viewTop() + s.grid().viewHeight();
    for (int i = 0; i < 2000; ++i) step(s, pointer(400, bottom - 1, i == 0), 16);
    const gfx::Rect last = s.grid().tileRect(59, s.grid().targetScroll());
    CHECK(last.y + last.h <= bottom);
    CHECK(last.y + last.h >= bottom - 20.0f);  // die letzte Reihe ist ganz zu sehen, nicht weiter
}

TEST_CASE_FIXTURE(Fixture, "Favoriten gemischt: Kachel im Cover-Format, Reihe so hoch wie die höchste (M24)") {
    ctx.width = 854;
    const MixedGames mixed;
    const Game* favs[] = {&mixed.games[6], &mixed.games[0], &mixed.games[3]};  // N64, NES, GB
    MainScene s(Library{mixed.games, favs, {}});
    step(s, InputFrame{});
    s.selectBarForTest(0);  // Favoriten
    REQUIRE(std::string(s.barLabelText(0)) == std::string(tr(Str::Favorites)));
    const gfx::Rect n64 = s.grid().tileRect(0, 0), nes = s.grid().tileRect(1, 0), gb = s.grid().tileRect(2, 0);
    CHECK(n64.h == 90.0f);
    CHECK(nes.h == 179.0f);
    CHECK(gb.h == 128.0f);
    CHECK(s.grid().rowHeight(0) == 179.0f);
    CHECK(n64.y + n64.h / 2 == doctest::Approx(nes.y + nes.h / 2));
    CHECK(gb.y + gb.h / 2 == doctest::Approx(nes.y + nes.h / 2));
}

TEST_CASE("tagAlpha: Konsolen-Plakette blendet mit dem Cover aus, Disc bleibt (M24)") {
    CHECK(tagAlpha(false, 0.0f) == doctest::Approx(1.0f));
    CHECK(tagAlpha(false, 0.25f) == doctest::Approx(0.75f));
    CHECK(tagAlpha(false, 1.0f) == doctest::Approx(0.0f));
    CHECK(tagAlpha(false, 2.0f) == doctest::Approx(0.0f));
    CHECK(tagAlpha(true, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(Fixture, "sortLabel zeigt die Sortierung der Konsolenlisten (M24)") {
    const MixedGames mixed;
    MainScene s(mixed.library());
    CHECK(s.sortLabel() == std::string(tr(Str::SortOrder)) + ": " + tr(Str::SortTitle));
    s.setSort(SortOrder::Year, nullptr);
    CHECK(s.sortLabel() == std::string(tr(Str::SortOrder)) + ": " + tr(Str::SortYear));
}

TEST_CASE_FIXTURE(Fixture, "Fokus-Kachel liegt über Konsolen- und Infoleiste, andere Kacheln darunter") {
    const ManyGames many(3);
    MainScene s(many.library());
    test::FakeCovers covers;
    covers.views["nes/00"] = CoverView{7, 1.0f};  // fokussiert
    covers.views["nes/01"] = CoverView{8, 1.0f};
    s.setCovers(&covers);
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    // Letzter Quad über die volle Breite gehört zu einer Leiste (Hintergrund kommt zuerst).
    u32 lastBar = 0;
    const auto verts = dl.vertices();
    for (u32 q = 0; q < dl.quadCount(); ++q)
        if (verts[q * 4].x == 0.0f && verts[q * 4 + 1].x == doctest::Approx(ctx.width)) lastBar = q * 4;
    REQUIRE(lastBar > 0);
    u32 focused = 0, other = 0;
    for (const gfx::Batch& b : dl.batches()) {
        if (b.texture == 7) focused = b.firstVertex;
        if (b.texture == 8) other = b.firstVertex;
    }
    REQUIRE(focused > 0);
    REQUIRE(other > 0);
    CHECK(focused > lastBar);
    CHECK(other < lastBar);
}

TEST_CASE_FIXTURE(Fixture, "Fokus-Kachel ragt höchstens um ihren Vergrößerungsrand über das Raster") {
    const ManyGames many(3);
    MainScene s(many.library());
    test::FakeCovers covers;
    covers.views["nes/00"] = CoverView{7, 1.0f};
    s.setCovers(&covers);
    for (int i = 0; i < 30; ++i) step(s, InputFrame{});  // Fokus-Animation fertig
    dl.clear();
    s.draw(ctx, dl);
    const gfx::Batch* cover = nullptr;
    for (const gfx::Batch& b : dl.batches())
        if (b.texture == 7) cover = &b;
    REQUIRE(cover != nullptr);
    REQUIRE(cover->clipped);
    const auto tile = s.grid().tileRect(0, 0.0f);
    CHECK(cover->clip.y < s.grid().viewTop());                       // darf in die Konsolenleiste ragen …
    CHECK(cover->clip.y >= s.grid().viewTop() - tile.h * 0.08f);     // … aber nur um den Rand
    CHECK(cover->clip.y + cover->clip.h <= doctest::Approx(s.grid().viewTop() + s.grid().viewHeight()));
}

TEST_CASE_FIXTURE(Fixture, "Untere Taskbar überlagert Spielauswahl bei Zeiger, Fokus-Cover bleibt über der Leiste") {
    const ManyGames many(20);
    MainScene s(many.library());
    DetailsScene details;
    s.setDetails(&details, nullptr);
    step(s, InputFrame{});
    const float viewBottom = s.grid().viewTop() + s.grid().viewHeight();
    // Zeiger knapp über der Taskbar auf einer Kachel der unteren Reihe → Kachel fokussiert
    const gfx::Rect r = s.grid().tileRect(6, 0.0f);
    const float tileY = r.y + r.h < viewBottom ? r.y + r.h - 2.0f : viewBottom - 2.0f;
    step(s, pointer(r.x + r.w / 2, tileY));
    CHECK(s.focusIndex() == 6);
    // Zeiger wandert in die untere Taskbar (leere Fläche) → Kachel wird abgewählt, Confirm öffnet kein Spiel
    step(s, pointer(ctx.width / 2, viewBottom + 4.0f));
    CHECK(s.focusIndex() == -1);
    CHECK(step(s, pointerAction(ctx.width / 2, viewBottom + 4.0f, Action::Confirm)).push == nullptr);
    // Zahnrad in der Taskbar lässt sich direkt treffen
    const gfx::Rect gear = s.gearRect(ctx);
    step(s, pointer(gear.x + gear.w / 2, gear.y + gear.h / 2));
    CHECK(step(s, pointerAction(gear.x + gear.w / 2, gear.y + gear.h / 2, Action::Confirm)).command ==
          AppCommand::CycleTheme);
}

TEST_CASE_FIXTURE(Fixture, "Platzhalter-Titeltext verschwindet sobald ein Cover geladen ist") {
    const Game games[] = {Game{Console::N64, "n64/a", "Super Langer Titel Der Sonst Hervorsteht", 0, 0}};
    MainScene s(Library{games, {}, {}});
    step(s, InputFrame{});
    dl.clear();
    s.draw(ctx, dl);
    const u32 withoutCover = dl.quadCount();

    test::FakeCovers covers;
    CoverView cv{7, 1.0f};
    cv.aspect = 0.7f;  // schmales/hohes Cover in breiter N64-Kachel
    covers.views["n64/a"] = cv;
    s.setCovers(&covers);
    dl.clear();
    s.draw(ctx, dl);
    const u32 withCover = dl.quadCount();
    // Ohne Cover: Hintergrund + Kachel + Platzhalter-Titeltext + Konsolen-Tag; mit Cover: Titeltext & Tag weg, +1 Cover
    CHECK(withCover < withoutCover);
}
