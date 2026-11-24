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

TEST_CASE("launchErrorText: EmuMissing → tr(Str::EmuMissing) mit Pfad") {
    const Texts t = render(LaunchFailure::EmuMissing, "sd:/apps/fceugx/boot.dol");
    CHECK(std::string(t.title) == tr(Str::LaunchFailed));
    CHECK(std::string(t.text) == "Emulator nicht gefunden:\nsd:/apps/fceugx/boot.dol");
}

TEST_CASE("launchErrorText: Fehlerart entscheidet, nicht die Dateiendung") {
    CHECK(std::string(render(LaunchFailure::EmuMissing, "sd:/apps/x/boot.elf").text) ==
          "Emulator nicht gefunden:\nsd:/apps/x/boot.elf");
    CHECK(std::string(render(LaunchFailure::EmuInvalid, "sd:/apps/x/boot.elf").text) ==
          "Emulator-Datei ist ungültig:\nsd:/apps/x/boot.elf");
}

TEST_CASE("launchErrorText: RomMissing → tr(Str::RomMissing)") {
    const Texts t = render(LaunchFailure::RomMissing, "sd:/roms/nes/Zelda.nes");
    CHECK(std::string(t.text) == "Spieldatei nicht gefunden:\nsd:/roms/nes/Zelda.nes");
}

TEST_CASE("launchErrorText: EmuInvalid → tr(Str::EmuInvalid), ArgsTooLong → tr(Str::ArgsTooLong)") {
    CHECK(std::string(render(LaunchFailure::EmuInvalid, "sd:/apps/x/boot.DOL").text) == "Emulator-Datei ist ungültig:\nsd:/apps/x/boot.DOL");
    CHECK(std::string(render(LaunchFailure::ArgsTooLong, "Argumente zu lang").text) == tr(Str::ArgsTooLong));
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
    CHECK(std::string(t.text) == tr(Str::NoN64Path));
}

TEST_CASE("launchErrorText: Nintendont-Fehler in DE und EN") {
    const Lang saved = language();
    for (Lang l : {Lang::De, Lang::En}) {
        setLanguage(l);
        const std::string old = render(LaunchFailure::NinTooOld, "6.480").text;
        CHECK(old.find("6.480") != std::string::npos);
        CHECK(old.find("6.493") != std::string::npos);
        CHECK(std::string(render(LaunchFailure::NinMissing, "sd:/apps/nintendont/boot.dol").text).find("sd:/apps/nintendont/boot.dol") !=
              std::string::npos);
        CHECK(std::string(render(LaunchFailure::NinUnknown, "sd:/x.dol").text).find("sd:/x.dol") != std::string::npos);
        CHECK(std::string(render(LaunchFailure::NinPathTooLong, "/games/a").text).find("/games/a") != std::string::npos);
        const std::string ahb = render(LaunchFailure::AhbProt, "").text;
        CHECK(ahb.find("Homebrew Channel") != std::string::npos);
        CHECK(ahb.find("%s") == std::string::npos);
    }
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::NinTooOld, "6.480").text) ==
          "Nintendont zu alt.\nGefunden 6.480, nötig 6.493 oder neuer.\nAktualisieren: Einstellungen → Emulatoren");
    // Spec §5.4: Preflight verweist auf die Aktualisierung
    CHECK(std::string(render(LaunchFailure::NinMissing, "sd:/apps/nintendont/boot.dol").text).find("Einstellungen → Emulatoren") !=
          std::string::npos);
    setLanguage(saved);
}

TEST_CASE("launchErrorText: Wii-Fehler in DE und EN") {
    const Lang saved = language();
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::WiiFsUnsupported, "NTFS").text) ==
          "Dateisystem nicht unterstützt.\nWii-Spiele brauchen FAT32 mit 512-Byte-Sektoren (NTFS).");
    CHECK(std::string(render(LaunchFailure::CiosMissing, "222").text) ==
          "cIOS fehlt.\nFür dieses Spiel wird ein cIOS in Slot 222 benötigt (d2x empfohlen).");
    CHECK(std::string(render(LaunchFailure::WiiFileBroken, "usb:/wbfs/a.wbfs").text) ==
          "Spieldatei fehlerhaft.\nusb:/wbfs/a.wbfs ist beschädigt oder zu stark fragmentiert.");
    setLanguage(Lang::En);
    CHECK(std::string(render(LaunchFailure::WiiFsUnsupported, "NTFS").text) ==
          "File system not supported.\nWii games need FAT32 with 512-byte sectors (NTFS).");
    CHECK(std::string(render(LaunchFailure::CiosMissing, "249").text) ==
          "cIOS missing.\nThis game needs a cIOS in slot 249 (d2x recommended).");
    CHECK(std::string(render(LaunchFailure::WiiFileBroken, "a.wbfs").text) == "Game file damaged.\na.wbfs is damaged or too fragmented.");
    setLanguage(saved);
}

TEST_CASE("launchErrorText: FileNotContiguousReadable bittet ums Umkopieren (DE/EN, M16)") {
    const Lang saved = language();
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::FileNotContiguousReadable, "usb2:/wii/a.iso").text) ==
          "Datei nicht direkt lesbar.\nDatei ist komprimiert oder lückenhaft – bitte unkomprimiert kopieren (usb2:/wii/a.iso).");
    setLanguage(Lang::En);
    CHECK(std::string(render(LaunchFailure::FileNotContiguousReadable, "usb2:/wii/a.iso").text) ==
          "File not directly readable.\nThe file is compressed or sparse – please copy it uncompressed (usb2:/wii/a.iso).");
    setLanguage(saved);
}

TEST_CASE("launchErrorText: CheatsTooLarge nennt die Größe in Bytes (DE/EN)") {
    const Lang saved = language();
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::CheatsTooLarge, "4000").text) ==
          "Zu viele Cheats.\nDie gewählten Codes belegen 4000 Bytes, zu viel für die Codeliste. Bitte weniger Codes wählen.");
    setLanguage(Lang::En);
    CHECK(std::string(render(LaunchFailure::CheatsTooLarge, "4000").text) ==
          "Too many cheats.\nThe selected codes take 4000 bytes, too much for the code list. Please select fewer codes.");
    setLanguage(saved);
}

TEST_CASE("launchWarningText: GC-Ports fehlen, Hinweis vor dem Start in DE und EN") {
    const Lang saved = language();
    char title[64];
    char text[256];
    setLanguage(Lang::De);
    launchWarningText({LaunchWarning::MemcardRealNoPorts}, title, sizeof title, text, sizeof text);
    CHECK(std::string(title) == "Hinweis");
    CHECK(std::string(text).find("Speicherkarte") != std::string::npos);
    CHECK(std::string(text).find("A erneut") != std::string::npos);
    launchWarningText({LaunchWarning::MemcardRealNoPorts, LaunchWarning::NativeSiNoPorts}, title, sizeof title, text,
                      sizeof text);
    CHECK(std::string(text).find("Controller") != std::string::npos);
    setLanguage(Lang::En);
    launchWarningText({LaunchWarning::NativeSiNoPorts}, title, sizeof title, text, sizeof text);
    CHECK(std::string(title) == "Note");
    CHECK(std::string(text).find("Press A again") != std::string::npos);
    setLanguage(saved);
}

TEST_CASE("M16: NintendontNeedsFat und Hinweis EmulatorFs mit Emulatorname (DE/EN)") {
    const Lang saved = language();
    char title[64];
    char text[320];
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::NintendontNeedsFat, "USB 2").text) ==
          "Nintendont braucht FAT32.\nGameCube-Spiele auf eine FAT32-Partition kopieren (USB 2).");
    launchWarningText({LaunchWarning::EmulatorFs}, title, sizeof title, text, sizeof text, "Snes9x GX");
    CHECK(std::string(text).find("Snes9x GX liest dieses Dateisystem eventuell nicht.") == 0);
    setLanguage(Lang::En);
    CHECK(std::string(render(LaunchFailure::NintendontNeedsFat, "USB 1").text) ==
          "Nintendont needs FAT32.\nCopy GameCube games to a FAT32 partition (USB 1).");
    launchWarningText({LaunchWarning::EmulatorFs}, title, sizeof title, text, sizeof text, "Snes9x GX");
    CHECK(std::string(text).find("Snes9x GX may not read this file system.") == 0);
    setLanguage(saved);
}

TEST_CASE("M19: DiscChanged und DiscUnreadable in DE und EN") {
    const Lang saved = language();
    setLanguage(Lang::De);
    CHECK(std::string(render(LaunchFailure::DiscChanged, "RSBP01").text).find("RSBP01") != std::string::npos);
    CHECK(std::string(render(LaunchFailure::DiscUnreadable, "RMCP01").text).find("nicht lesbar") != std::string::npos);
    setLanguage(Lang::En);
    CHECK(std::string(render(LaunchFailure::DiscChanged, "RSBP01").text).find("RSBP01") != std::string::npos);
    CHECK(std::string(render(LaunchFailure::DiscUnreadable, "RMCP01").text).find("not readable") != std::string::npos);
    setLanguage(saved);
}
