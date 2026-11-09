#include "ui/debug_overlay.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::ui;

namespace {
const gfx::AtlasShapes kShapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};
}

TEST_CASE("DebugOverlay ist anfangs unsichtbar und zeichnet nichts; toggle macht sichtbar") {
    gfx::FontSet fonts = test::fakeFontSet();
    UiContext ctx;
    ctx.theme = &darkTheme();
    ctx.fonts = &fonts;
    ctx.shapes = &kShapes;
    gfx::DrawList dl(512, 4);

    DebugOverlay o;
    CHECK_FALSE(o.visible());
    o.setStats(FrameStats{4200, 1, 310, 0, 524288});
    o.draw(ctx, dl);
    CHECK(dl.quadCount() == 0);

    o.toggle();
    CHECK(o.visible());
    o.draw(ctx, dl);
    CHECK(dl.quadCount() > 10);
    CHECK(dl.droppedQuads() == 0);
    o.toggle();
    CHECK_FALSE(o.visible());
}
