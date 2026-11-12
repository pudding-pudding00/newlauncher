#include "ui/sysinfo_scene.hpp"
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

SystemInfo info() {
    SystemInfo s;
    s.platform = Platform::Wii;
    s.sysMenuVersion = 513;
    s.region = 'U';
    s.launcherVersion = "1.2.3";
    s.launcherIos = 58;
    for (int i = 0; i < 30; ++i) s.devices.push_back(DeviceSummary{"usb" + std::to_string(i), "FAT", 0, 0, 1});
    return s;
}

struct Fixture {
    gfx::FontSet fonts = test::fakeFontSet(8, 16);
    UiContext ctx;
    gfx::DrawList dl{16384, 16};
    SysInfoScene s;
    Fixture() {
        ctx.theme = &lightTheme();
        ctx.fonts = &fonts;
        ctx.shapes = &kShapes;
        s.setInfo(info());
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

bool anyLineContains(const SysInfoScene& s, std::string_view needle) {
    for (const std::string& l : s.lines())
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE_FIXTURE(Fixture, "Zeilen in der aktiven Sprache (DE/EN)") {
    LangGuard g;
    setLanguage(Lang::De);
    s.onEnter();
    CHECK(anyLineContains(s, "Plattform: Wii"));
    CHECK(anyLineContains(s, "System-Menü: 4.3U"));
    setLanguage(Lang::En);
    s.onEnter();
    CHECK(anyLineContains(s, "Platform: Wii"));
    CHECK(anyLineContains(s, "System Menu: 4.3U"));
}

TEST_CASE_FIXTURE(Fixture, "A fordert Speichern an, B schließt") {
    SceneRequests r = press(Action::Confirm);
    CHECK(r.command == AppCommand::SaveSysInfo);
    CHECK_FALSE(r.pop);
    r = press(Action::Back);
    CHECK(r.pop);
}

TEST_CASE_FIXTURE(Fixture, "Blättern bis zum Ende, nicht weiter") {
    CHECK(s.maxScroll(ctx) > 0);
    for (int i = 0; i < 100; ++i) press(Action::Down);
    CHECK(s.scroll() == s.maxScroll(ctx));
    for (int i = 0; i < 100; ++i) press(Action::Up);
    CHECK(s.scroll() == 0);
}

TEST_CASE_FIXTURE(Fixture, "Ergebnis des Speicherns erscheint als Status; onEnter löscht ihn") {
    LangGuard g;
    setLanguage(Lang::De);
    CHECK(s.status().empty());
    s.setSaveResult("sd:/newlauncher/sysinfo.txt", true);
    CHECK(s.status() == "Gespeichert: sd:/newlauncher/sysinfo.txt");
    s.setSaveResult("sd:/newlauncher/sysinfo.txt", false);
    CHECK(s.status() == "Speichern fehlgeschlagen");
    s.onEnter();
    CHECK(s.status().empty());
    s.draw(ctx, dl);
}
