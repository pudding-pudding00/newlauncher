#include "ui/about_scene.hpp"
#include "ui/strings.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
#include <string_view>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};

struct LangGuard {
    Lang saved = language();
    ~LangGuard() { setLanguage(saved); }
};

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{16384, 16};
    AboutScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        s.setBuildInfo("1.2.3", false);
        s.onEnter();
    }
    SceneRequests press(Action a) {
        InputFrame f;
        f.actions = 1u << static_cast<u32>(a);
        SceneRequests r;
        s.update(ctx, f, 16, r);
        return r;
    }
};

bool anyLineContains(const AboutScene& s, std::string_view needle) {
    for (const std::string& l : s.lines())
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("Komponentenliste kommt aus THIRD_PARTY.md") {
    const auto entries = aboutEntries();
    REQUIRE(entries.size() >= 10);
    bool freetype = false, mbedtls = false, font = false;
    for (const AboutEntry& e : entries) {
        CHECK(std::string_view(e.name).size() > 0);
        CHECK(std::string_view(e.license).size() > 0);
        freetype |= std::string_view(e.name) == "FreeType";
        mbedtls |= std::string_view(e.name) == "Mbed TLS";
        font |= std::string_view(e.name).find("M PLUS") != std::string_view::npos;
    }
    CHECK(freetype);
    CHECK(mbedtls);
    CHECK(font);
}

TEST_CASE_FIXTURE(Fixture, "Kopfzeilen: Version, HTTPS-Schalter, GPL-Satz (DE/EN)") {
    LangGuard g;
    setLanguage(Lang::De);
    s.onEnter();
    CHECK(anyLineContains(s, "newlauncher 1.2.3"));
    CHECK(anyLineContains(s, "HTTPS: Aus"));
    CHECK(anyLineContains(s, "GNU GPL"));
    CHECK(anyLineContains(s, "FreeType"));
    s.setBuildInfo("1.2.3", true);
    setLanguage(Lang::En);
    s.onEnter();
    CHECK(anyLineContains(s, "HTTPS: On"));
    CHECK(anyLineContains(s, "GNU GPL"));
}

TEST_CASE_FIXTURE(Fixture, "B und Home schließen") {
    CHECK(press(Action::Back).pop);
    CHECK(press(Action::Home).pop);
}

TEST_CASE_FIXTURE(Fixture, "Blättern bleibt in den Grenzen") {
    CHECK(s.scroll() == 0);
    press(Action::Up);
    CHECK(s.scroll() == 0);
    press(Action::Down);
    CHECK(s.scroll() == 1);
    for (int i = 0; i < 200; ++i) press(Action::Down);
    CHECK(s.scroll() == s.maxScroll(ctx));
    CHECK(s.maxScroll(ctx) > 0);  // die Liste ist länger als die Karte
}

TEST_CASE_FIXTURE(Fixture, "Zeichnen erzeugt Quads, Overlay") {
    CHECK(s.isOverlay());
    s.draw(ctx, dl);
    CHECK(dl.quadCount() > 0);
}
