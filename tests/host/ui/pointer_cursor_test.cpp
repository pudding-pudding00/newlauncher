#include "ui/pointer_cursor.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::ui;

TEST_CASE("Zeiger-Cursor: sichtbar nur bei gültigem Zeiger, zentriert auf der Position") {
    const gfx::AtlasShapes shapes{gfx::UvRect{0, 0, 0.01f, 0.01f}, gfx::UvRect{0.5f, 0.5f, 0.6f, 0.6f}};
    UiContext ctx;
    ctx.theme = &lightTheme();
    ctx.shapes = &shapes;
    gfx::DrawList dl(64, 4);
    InputFrame in;
    drawPointerCursor(ctx, dl, in);
    CHECK(dl.quadCount() == 0);
    in.pointerValid = true;
    in.pointerX = 200;
    in.pointerY = 150;
    drawPointerCursor(ctx, dl, in);
    REQUIRE(dl.quadCount() >= 2);
    float minX = 1e9f, maxX = -1e9f;
    for (const gfx::Vertex& v : dl.vertices()) {
        minX = v.x < minX ? v.x : minX;
        maxX = v.x > maxX ? v.x : maxX;
    }
    CHECK((minX + maxX) / 2 == doctest::Approx(200));
}
