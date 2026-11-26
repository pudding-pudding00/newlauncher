#include "core/library_store.hpp"
#include "ui/details_scene.hpp"
#include "ui/main_scene.hpp"
#include "ui/test_library.hpp"
#include "support/fake_covers.hpp"
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

TEST_CASE("detailsDeviceLine: nur bei mehreren Geräten, mit „auch auf“") {
    const std::vector<std::string> one{"sd"}, three{"usb", "usb2", "sd"};
    setLanguage(Lang::De);
    CHECK(detailsDeviceLine(one, 0, {}).empty());
    CHECK(detailsDeviceLine(three, 1, {}) == "Gerät: USB 2");
    const std::vector<u8> also{0, 2};
    CHECK(detailsDeviceLine(three, 1, also) == "Gerät: USB 2 (auch auf: USB 1, SD)");
    setLanguage(Lang::En);
    CHECK(detailsDeviceLine(three, 0, std::vector<u8>{2}) == "Device: USB 1 (also on: SD)");
    setLanguage(Lang::De);
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
    CHECK(std::string(buf) == std::string(tr(Str::OverrideIgnored)) + " VC-Inject: nicht zugeordnet · Wii64: DOL fehlt");
    n64HintLine(decision(true, true, true), buf, sizeof buf);
    CHECK(std::string(buf).empty());
}

TEST_CASE_FIXTURE(Fixture, "Details: großes Cover aus dem Provider, angefordert mit Visible") {
    DetailsScene d;
    test::FakeCovers covers;
    covers.views["n64/sm64.z64"] = CoverView{9, 1.0f};
    d.setCovers(&covers);
    const Game g{Console::N64, "n64/sm64.z64", "Super Mario 64", 1996, 1};
    d.open(g, false, 0);
    step(d, InputFrame{});
    CHECK(covers.wants["n64/sm64.z64"].visible == 1);
    CHECK(covers.wants["n64/sm64.z64"].prefetch == 0);
    dl.clear();
    d.draw(ctx, dl);
    const gfx::Batch* cover = nullptr;
    for (const gfx::Batch& b : dl.batches())
        if (b.texture == 9) cover = &b;
    REQUIRE(cover != nullptr);
    const gfx::Vertex& tl = dl.vertices()[cover->firstVertex];
    const gfx::Vertex& br = dl.vertices()[cover->firstVertex + 2];
    CHECK(br.x - tl.x == doctest::Approx(200.0));
    CHECK(br.y - tl.y == doctest::Approx(280.0));
    CHECK(tl.color.a == 255);
}

TEST_CASE_FIXTURE(Fixture, "Klänge: A auf Cover öffnet Details mit Open; Details: rechts Move, B Back, A Confirm") {
    const Library lib = testLibrary();
    MainScene s(lib);
    DetailsScene d;
    s.setDetails(&d, nullptr);
    step(s, InputFrame{});
    CHECK(step(s, action(Action::Confirm)).sound == UiSound::Open);
    d.onEnter();
    CHECK(step(d, action(Action::Right)).sound == UiSound::Move);
    CHECK(step(d, action(Action::Confirm)).sound == UiSound::Confirm);
    CHECK(step(d, action(Action::Back)).sound == UiSound::Back);
}

TEST_CASE_FIXTURE(Fixture, "Details: + öffnet die Optionen nur für Wii und GameCube (M14)") {
    DetailsScene d;
    const Game wii{Console::Wii, "RMGP01", "Super Mario Galaxy", 2007, 1};
    d.open(wii, false, 0);
    SceneRequests r = step(d, action(Action::Options));
    CHECK(r.command == AppCommand::OpenOptions);
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->key) == "RMGP01");
    const Game nes{Console::Nes, "nes/a.nes", "A", 0, 0};
    d.open(nes, false, 0);
    CHECK(step(d, action(Action::Options)).command == AppCommand::None);
}

TEST_CASE_FIXTURE(Fixture, "Details: Hoch fokussiert „Cheats“ bei Wii und GC, A öffnet die Cheat-Szene (M15)") {
    DetailsScene d;
    const Game gc{Console::Gc, "GM8P01", "Metroid Prime", 2002, 1};
    d.open(gc, false, 0);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);
    SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::OpenCheats);
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->key) == "GM8P01");
    step(d, action(Action::Down));
    CHECK(d.focusedButton() == 0);
    const Game nes{Console::Nes, "nes/a.nes", "A", 0, 0};
    d.open(nes, false, 0);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == 0);
    CHECK(std::string(appCommandName(AppCommand::OpenCheats)) == "open_cheats");
}

// --- M26: Zeile „Spielstand …“ rechts neben „Cheats“ ---
TEST_CASE_FIXTURE(Fixture, "Details: Spielstand-Zeile nur mit setHasSave, Rechts von Cheats, A öffnet die Saves (M26)") {
    DetailsScene d;
    const Game wii{Console::Wii, "RMGP01", "Super Mario Galaxy", 2007, 1};
    d.open(wii, false, 0);
    step(d, action(Action::Up));
    step(d, action(Action::Right));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);  // ohne Spielstand keine Zeile
    d.setHasSave(true);
    step(d, action(Action::Right));
    CHECK(d.focusedButton() == DetailsScene::kFocusSave);
    SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::OpenSaves);
    REQUIRE(r.game != nullptr);
    CHECK(std::string(r.game->key) == "RMGP01");
    step(d, action(Action::Left));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);
    step(d, action(Action::Right));
    step(d, action(Action::Down));
    CHECK(d.focusedButton() == 0);
    d.open(wii, false, 0);  // open() setzt zurück
    step(d, action(Action::Up));
    step(d, action(Action::Right));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);
    CHECK(std::string(appCommandName(AppCommand::OpenSaves)) == "open_saves");
}

TEST_CASE_FIXTURE(Fixture, "Details: Spielstand-Zeile ohne Cheats ist die obere Zeile; Zeiger trifft sie (M26)") {
    DetailsScene d;
    const Game gb{Console::Gb, "gb/a.gb", "A", 0, 0};
    d.open(gb, false, 0);
    d.setHasSave(true);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusSave);
    InputFrame f;
    f.pointerValid = true;
    f.pointerMoved = true;
    const gfx::Rect r = d.saveRect(ctx);
    f.pointerX = r.x + r.w / 2;
    f.pointerY = r.y + r.h / 2;
    f.actions = 1u << static_cast<u32>(Action::Confirm);
    d.onEnter();
    CHECK(step(d, f).command == AppCommand::OpenSaves);
    d.draw(ctx, dl);  // zeichnet ohne Absturz
}

// --- M17: Zeile „Kategorien“ (Häkchenliste) ---
TEST_CASE_FIXTURE(Fixture, "Details: Kategorien – Hoch wählt die Zeile, Links/Rechts blättern, A schaltet um") {
    DetailsScene d;
    const Game g{Console::Nes, "nes/a.nes", "A", 0, 0};
    d.open(g, false, 0);
    d.setCategories({"Party", "Retro"}, {false, true});
    step(d, action(Action::Up));
    REQUIRE(d.focusedButton() == DetailsScene::kFocusCategories);
    CHECK(d.categoryIndex() == 0);
    const SceneRequests r = step(d, action(Action::Confirm));
    CHECK(r.command == AppCommand::ToggleCategory);
    CHECK(r.category == 0);
    REQUIRE(r.game != nullptr);
    CHECK(std::strcmp(r.game->key, "nes/a.nes") == 0);
    step(d, action(Action::Right));
    CHECK(d.categoryIndex() == 1);
    step(d, action(Action::Right));
    CHECK(d.categoryIndex() == 0);
    step(d, action(Action::Left));
    CHECK(d.categoryIndex() == 1);
    d.setCategories({"Party", "Retro"}, {true, false});  // nach dem Umschalten: Auswahl bleibt
    CHECK(d.categoryIndex() == 1);
    CHECK_FALSE(d.categoryChecked(1));
    CHECK(d.categoryChecked(0));
    step(d, action(Action::Down));
    CHECK(d.focusedButton() == 0);
    d.draw(ctx, dl);
}

TEST_CASE_FIXTURE(Fixture, "Details: Kategorien liegen zwischen Knöpfen und Cheats; open() leert sie") {
    DetailsScene d;
    const Game g{Console::Wii, "RMCP01", "Mario Kart", 2008, 4};
    d.open(g, false, 0);
    d.setCategories({"Party"}, {false});
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusCategories);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);
    step(d, action(Action::Down));
    CHECK(d.focusedButton() == DetailsScene::kFocusCategories);
    d.open(g, false, 0);
    step(d, action(Action::Up));
    CHECK(d.focusedButton() == DetailsScene::kFocusCheats);
}

TEST_CASE("detailsStatsLine: DE/EN, einmal, nie gestartet; Datum je Sprache") {
    char buf[128];
    setLanguage(Lang::De);
    detailsStatsLine(PlayStat{12, 1762128000u}, buf, sizeof buf);  // 2025-11-03
    CHECK(std::string(buf) == "12-mal gestartet, zuletzt am 03.11.2025");
    detailsStatsLine(PlayStat{1, 0}, buf, sizeof buf);
    CHECK(std::string(buf) == "Einmal gestartet");
    detailsStatsLine(PlayStat{0, 5}, buf, sizeof buf);
    CHECK(std::string(buf).empty());
    setLanguage(Lang::En);
    detailsStatsLine(PlayStat{12, 1762128000u}, buf, sizeof buf);
    CHECK(std::string(buf) == "Started 12 times, last on 2025-11-03");
    detailsStatsLine(PlayStat{1, 1762128000u}, buf, sizeof buf);
    CHECK(std::string(buf) == "Started once, last on 2025-11-03");
    detailsInfoLine(Game{Console::N64, "n64/x", "X", 0, 0}, 1762128000u, buf, sizeof buf);
    CHECK(std::string(buf).find("2025-11-03") != std::string::npos);
    setLanguage(Lang::De);
}

TEST_CASE("DetailsScene::setStats: mit Starts entfällt das Verlaufsdatum in der Infozeile") {
    DetailsScene d;
    d.open(Game{Console::Nes, "nes/x", "X", 0, 0}, false, 1762128000u);
    CHECK(std::string(d.infoText()).find("03.11.2025") != std::string::npos);
    CHECK(std::string(d.statsText()).empty());
    d.setStats(PlayStat{3, 1762128000u});
    CHECK(std::string(d.infoText()).find("03.11.2025") == std::string::npos);
    CHECK(std::string(d.statsText()) == "3-mal gestartet, zuletzt am 03.11.2025");
    d.setStats(PlayStat{});
    CHECK(std::string(d.infoText()).find("03.11.2025") != std::string::npos);
}

// --- M18: GC-Banner unter dem Cover ---
namespace {
class FakeBanners final : public BannerProvider {
public:
    void want(const Game& g) override { wanted.emplace_back(g.key); }
    BannerView view(const Game&) const override { return v; }
    std::vector<std::string> wanted;
    BannerView v;
};
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Details: GC-Spiel möchte ein Banner und zeichnet es 2× unter dem Cover") {
    FakeBanners b;
    DetailsScene d;
    d.setBanners(&b);
    d.open(Game{Console::Gc, "GM8P01", "Metroid Prime", 0, 1}, false, 0);
    step(d, InputFrame{});
    REQUIRE(b.wanted.size() == 1);
    CHECK(b.wanted[0] == "GM8P01");
    ctx.width = 640;
    ctx.height = 480;
    dl.clear();
    d.draw(ctx, dl);
    CHECK(dl.batches().size() > 0);
    for (const gfx::Batch& bt : dl.batches()) CHECK(bt.texture != gfx::kBannerTextureBase);  // noch nicht geladen
    b.v = BannerView{gfx::kBannerTextureBase, 96, 32};
    dl.clear();
    d.draw(ctx, dl);
    bool found = false;
    for (const gfx::Batch& bt : dl.batches()) {
        if (bt.texture != gfx::kBannerTextureBase) continue;
        found = true;
        REQUIRE(bt.vertexCount == 4);
        const gfx::Vertex* v = &dl.vertices()[bt.firstVertex];
        CHECK(v[2].x - v[0].x == doctest::Approx(192));
        CHECK(v[2].y - v[0].y == doctest::Approx(64));
        CHECK(v[0].y >= 32 + 280);   // unter dem Cover
        CHECK(v[2].y <= 480 - 72);   // über der Infoleiste
    }
    CHECK(found);
}

TEST_CASE_FIXTURE(Fixture, "Details: jede Konsole fragt an, der Dienst entscheidet (VC-Kanal-Banner, M18 Task 8)") {
    FakeBanners b;
    DetailsScene d;
    d.setBanners(&b);
    d.open(Game{Console::Nes, "nes/a", "A", 0, 0}, false, 0);
    step(d, InputFrame{});
    REQUIRE(b.wanted.size() == 1);
    CHECK(b.wanted[0] == "nes/a");
}

TEST_CASE_FIXTURE(Fixture, "Details: Kanal-Banner-Bild ersetzt das Cover, 4:3 im Cover-Feld, mit Clip (M18)") {
    FakeBanners b;
    DetailsScene d;
    d.setBanners(&b);
    d.open(Game{Console::Gc, "GM8P01", "Metroid Prime", 0, 1}, false, 0);
    step(d, InputFrame{});
    Quad q;
    q.pos = {Vec2{-304, 228}, Vec2{304, 228}, Vec2{-304, -228}, Vec2{304, -228}};  // ganzes Layout
    q.uv = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}, Vec2{1, 1}};
    q.color = {Color{255, 255, 255, 255}, Color{255, 255, 255, 255}, Color{255, 255, 255, 255}, Color{255, 255, 255, 255}};
    q.texIndex = 2;
    const Quad quads[1] = {q};
    const BannerFrame frame{608, 456, quads};
    b.v = BannerView{0, 608, 456, &frame};
    ctx.width = 640;
    ctx.height = 480;
    dl.clear();
    d.draw(ctx, dl);
    bool found = false;
    for (const gfx::Batch& bt : dl.batches()) {
        if (bt.texture != gfx::kBannerTextureBase + 2) continue;
        found = true;
        CHECK(bt.clipped);
        const gfx::Vertex* v = &dl.vertices()[bt.firstVertex];
        CHECK(v[0].x == doctest::Approx(32));           // Cover-Feld links (kMargin)
        CHECK(v[1].x - v[0].x == doctest::Approx(200));  // Breite des Cover-Felds
        CHECK(v[2].y - v[0].y == doctest::Approx(150));  // 4:3
        CHECK(v[0].y == doctest::Approx(32 + 65));       // senkrecht mittig: (280 − 150) / 2
    }
    CHECK(found);
}
