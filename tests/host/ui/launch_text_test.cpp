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
Texts render(LaunchFailure kind, const char* context) {
    Texts t{};
    launchErrorText(LaunchError{kind, context}, t.title, sizeof t.title, t.text, sizeof t.text);
    return t;
}
}  // namespace

TEST_CASE("launchErrorText: EmuMissing → kStrEmuMissing mit Pfad") {
    const Texts t = render(LaunchFailure::EmuMissing, "sd:/apps/fceugx/boot.dol");
    CHECK(std::string(t.title) == kStrLaunchFailed);
    CHECK(std::string(t.text) == "Emulator nicht gefunden:\nsd:/apps/fceugx/boot.dol");
}

TEST_CASE("launchErrorText: Fehlerart entscheidet, nicht die Dateiendung") {
    CHECK(std::string(render(LaunchFailure::EmuMissing, "sd:/apps/x/boot.elf").text) ==
          "Emulator nicht gefunden:\nsd:/apps/x/boot.elf");
    CHECK(std::string(render(LaunchFailure::EmuInvalid, "sd:/apps/x/boot.elf").text) ==
          "Emulator-Datei ist ungültig:\nsd:/apps/x/boot.elf");
}

TEST_CASE("launchErrorText: RomMissing → kStrRomMissing") {
    const Texts t = render(LaunchFailure::RomMissing, "sd:/roms/nes/Zelda.nes");
    CHECK(std::string(t.text) == "Spieldatei nicht gefunden:\nsd:/roms/nes/Zelda.nes");
}

TEST_CASE("launchErrorText: EmuInvalid → kStrEmuInvalid, ArgsTooLong → kStrArgsTooLong") {
    CHECK(std::string(render(LaunchFailure::EmuInvalid, "sd:/apps/x/boot.DOL").text) == "Emulator-Datei ist ungültig:\nsd:/apps/x/boot.DOL");
    CHECK(std::string(render(LaunchFailure::ArgsTooLong, "Argumente zu lang").text) == kStrArgsTooLong);
}

TEST_CASE("launchErrorText: Unsupported → Konsolenname im Text") {
    const Texts t = render(LaunchFailure::Unsupported, "Nintendo 64");
    CHECK(std::string(t.text) == "Starten für Nintendo 64 folgt in einer späteren Version.");
}

TEST_CASE("Text länger als Puffer wird mit … gekürzt, nie überlaufen") {
    char title[64];
    char text[24 + 4];
    std::memset(text + 24, 0x5A, 4);  // Wächter hinter dem erlaubten Bereich
    const std::string path = "sd:/roms/nes/" + std::string(100, 'x') + ".nes";
    launchErrorText(LaunchError{LaunchFailure::RomMissing, path}, title, sizeof title, text, 24);
    const std::string s(text);
    CHECK(s.size() < 24);
    CHECK(s.size() >= 3);
    CHECK(s.substr(s.size() - 3) == "\xE2\x80\xA6");
    CHECK(static_cast<u8>(text[24]) == 0x5A);
    // UTF-8 wird nicht mitten im Zeichen zerschnitten: „ü“ aus „ungültig“ landet ganz oder gar nicht.
    char small[32];
    launchErrorText(LaunchError{LaunchFailure::EmuInvalid, "a.dol"}, title, sizeof title, small, 27);
    const std::string u(small);
    for (std::size_t i = 0; i + 3 < u.size(); ++i)
        if (static_cast<u8>(u[i]) == 0xC3) CHECK(static_cast<u8>(u[i + 1]) == 0xBC);
}

TEST_CASE("launchErrorText: NoN64Path") {
    const Texts t = render(LaunchFailure::NoN64Path, "Nintendo 64");
    CHECK(std::string(t.text) == kStrNoN64Path);
}
