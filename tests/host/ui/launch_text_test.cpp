#include "core/types.hpp"
#include "ui/launch_text.hpp"
#include "ui/strings.hpp"
#include <doctest/doctest.h>
#include <cstring>
#include <string>
using namespace nl;
using namespace nl::ui;

namespace {
struct Texts {
    char title[64];
    char text[256];
};
Texts render(ErrorCode code, const char* context) {
    Texts t{};
    launchErrorText(Error{code, context}, t.title, sizeof t.title, t.text, sizeof t.text);
    return t;
}
}  // namespace

TEST_CASE("launchErrorText: NotFound mit Pfad auf .dol → kStrEmuMissing mit Pfad") {
    const Texts t = render(ErrorCode::NotFound, "sd:/apps/fceugx/boot.dol");
    CHECK(std::string(t.title) == kStrLaunchFailed);
    CHECK(std::string(t.text) == "Emulator nicht gefunden:\nsd:/apps/fceugx/boot.dol");
}

TEST_CASE("launchErrorText: NotFound mit anderem Pfad → kStrRomMissing") {
    const Texts t = render(ErrorCode::NotFound, "sd:/roms/nes/Zelda.nes");
    CHECK(std::string(t.text) == "Spieldatei nicht gefunden:\nsd:/roms/nes/Zelda.nes");
}

TEST_CASE("launchErrorText: InvalidData mit .dol → kStrEmuInvalid, sonst kStrArgsTooLong") {
    CHECK(std::string(render(ErrorCode::InvalidData, "sd:/apps/x/boot.DOL").text) == "Emulator-Datei ist ungültig:\nsd:/apps/x/boot.DOL");
    CHECK(std::string(render(ErrorCode::InvalidData, "Argumente zu lang").text) == kStrArgsTooLong);
}

TEST_CASE("launchErrorText: Unsupported → Konsolenname im Text") {
    const Texts t = render(ErrorCode::Unsupported, "Nintendo 64");
    CHECK(std::string(t.text) == "Starten für Nintendo 64 folgt in einer späteren Version.");
}

TEST_CASE("Text länger als Puffer wird mit … gekürzt, nie überlaufen") {
    char title[64];
    char text[24 + 4];
    std::memset(text + 24, 0x5A, 4);  // Wächter hinter dem erlaubten Bereich
    const std::string path = "sd:/roms/nes/" + std::string(100, 'x') + ".nes";
    launchErrorText(Error{ErrorCode::NotFound, path}, title, sizeof title, text, 24);
    const std::string s(text);
    CHECK(s.size() < 24);
    CHECK(s.size() >= 3);
    CHECK(s.substr(s.size() - 3) == "\xE2\x80\xA6");
    CHECK(static_cast<u8>(text[24]) == 0x5A);
    // UTF-8 wird nicht mitten im Zeichen zerschnitten: „ü“ aus „ungültig“ landet ganz oder gar nicht.
    char small[32];
    launchErrorText(Error{ErrorCode::InvalidData, "a.dol"}, title, sizeof title, small, 27);
    const std::string u(small);
    for (std::size_t i = 0; i + 3 < u.size(); ++i)
        if (static_cast<u8>(u[i]) == 0xC3) CHECK(static_cast<u8>(u[i + 1]) == 0xBC);
}
