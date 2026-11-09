#include "gfx/text.hpp"
#include "support/fake_font.hpp"
#include <doctest/doctest.h>
#include <string>
#include <string_view>
using namespace nl;
using namespace nl::gfx;

namespace {
const Color kC{1, 2, 3, 255};

std::string_view lineText(std::string_view s, Line l) { return s.substr(l.start, l.length); }
}  // namespace

TEST_CASE("measureText zählt Codepoints, nicht Bytes") {
    const FontFace f = test::fakeFace(10);
    CHECK(measureText(f, "Zäh") == doctest::Approx(30));
    CHECK(measureText(f, "") == 0);
}

TEST_CASE("drawText erzeugt ein Quad pro sichtbarem Glyph, keins für Leerzeichen") {
    const FontFace f = test::fakeFace(10);
    DrawList dl(64, 4);
    drawText(dl, f, 5, 100, "Ab c", kC);
    CHECK(dl.quadCount() == 3);
    // Erstes Glyph: x = 5 + bearingX (1), y = Grundlinie − bearingY (12).
    CHECK(dl.vertices()[0].x == doctest::Approx(6));
    CHECK(dl.vertices()[0].y == doctest::Approx(88));
    // Drittes Glyph steht nach drei Vorschüben.
    CHECK(dl.vertices()[8].x == doctest::Approx(36));
}

TEST_CASE("drawText rastet Glyphen auf ganze Bildpunkte ein, wenn pixelScaleX gesetzt ist") {
    FontFace f = test::fakeFace(10);
    f.pixelScaleX = 0.75f;  // 16:9: 854 logische auf 640 echte Pixel
    DrawList dl(64, 4);
    drawText(dl, f, 5.3f, 100, "AB", kC);
    for (int q = 0; q < 2; ++q) {
        const float px = dl.vertices()[q * 4].x * 0.75f;
        CHECK(px == doctest::Approx(static_cast<float>(static_cast<int>(px + 0.5f))).epsilon(0.0001));
    }
}

TEST_CASE("drawTextEllipsis lässt passenden Text unverändert") {
    const FontFace f = test::fakeFace(10);
    DrawList dl(64, 4);
    drawTextEllipsis(dl, f, 0, 20, 50, "ABCDE", kC);
    CHECK(dl.quadCount() == 5);
}

TEST_CASE("drawTextEllipsis kürzt und hängt … an") {
    const FontFace f = test::fakeFace(10);
    DrawList dl(64, 4);
    drawTextEllipsis(dl, f, 0, 20, 50, "ABCDEFGH", kC);
    CHECK(dl.quadCount() == 5);  // 4 Zeichen + „…"
    // Kein Quad endet rechts von maxWidth.
    for (const Vertex& v : dl.vertices()) CHECK(v.x <= 50.0f);
}

TEST_CASE("drawTextEllipsis mit maxWidth < Breite von … zeichnet nichts") {
    const FontFace f = test::fakeFace(10);
    DrawList dl(64, 4);
    drawTextEllipsis(dl, f, 0, 20, 5, "ABCDEFGH", kC);
    CHECK(dl.quadCount() == 0);
}

TEST_CASE("wrapText bricht an Leerzeichen") {
    const FontFace f = test::fakeFace(10);
    const std::string_view s = "The Legend of Zelda";
    Line lines[4];
    const int n = wrapText(f, s, 80, lines);
    REQUIRE(n == 3);
    CHECK(lineText(s, lines[0]) == "The");
    CHECK(lineText(s, lines[1]) == "Legend");
    CHECK(lineText(s, lines[2]) == "of Zelda");
}

TEST_CASE("wrapText bricht überlange Wörter hart") {
    const FontFace f = test::fakeFace(10);
    const std::string_view s = "Superlangeswort";
    Line lines[4];
    const int n = wrapText(f, s, 50, lines);
    REQUIRE(n == 3);
    CHECK(lineText(s, lines[0]) == "Super");
    CHECK(lineText(s, lines[1]) == "lange");
    CHECK(lineText(s, lines[2]) == "swort");
}

TEST_CASE("wrapText trennt nicht mitten in einer UTF-8-Folge") {
    const FontFace f = test::fakeFace(10);
    const std::string_view s = "ÄÄÄÄ";  // 4 Codepoints, 8 Bytes
    Line lines[4];
    const int n = wrapText(f, s, 30, lines);
    REQUIRE(n == 2);
    CHECK(lineText(s, lines[0]) == "ÄÄÄ");
    CHECK(lineText(s, lines[1]) == "Ä");
}

TEST_CASE("wrapText begrenzt auf out.size() Zeilen") {
    const FontFace f = test::fakeFace(10);
    std::string s;
    for (int i = 0; i < 40; ++i) s += "Wort ";
    s.pop_back();  // 199 Zeichen
    Line lines[2];
    const int n = wrapText(f, s, 80, lines);
    REQUIRE(n == 2);
    CHECK(lines[1].start + lines[1].length == s.size());
}

TEST_CASE("leerer String: Breite 0, 0 Quads, 0 Zeilen") {
    const FontFace f = test::fakeFace(10);
    DrawList dl(8, 2);
    drawText(dl, f, 0, 0, "", kC);
    drawTextEllipsis(dl, f, 0, 0, 100, "", kC);
    CHECK(dl.quadCount() == 0);
    Line lines[2];
    CHECK(wrapText(f, "", 100, lines) == 0);
    CHECK(wrapText(f, "   ", 100, lines) == 0);
}
