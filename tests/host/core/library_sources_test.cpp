#include "core/library_sources.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nl;

namespace {
LibraryRules rules(const char* text) { return libraryRulesFromIni(IniDoc::parse(text)); }
const ConsolePaths& paths(const LibraryRules& r, Console c) { return r.paths[static_cast<std::size_t>(c)]; }
using Strs = std::vector<std::string>;
}  // namespace

TEST_CASE("library_sources: Liste behält Reihenfolge, Leerzeichen getrimmt") {
    const LibraryRules r = rules("[library]\nsources =  usb2 , SD: \n");
    CHECK_FALSE(r.sources.autoMode);
    CHECK(r.sources.devices == Strs{"usb2", "sd"});
    CHECK(sourcesToText(r.sources) == "usb2,sd");
    CHECK(r.bad.empty());
}

TEST_CASE("library_sources: auto und Standard") {
    CHECK(rules("[library]\nsources = auto\n").sources.autoMode);
    CHECK(rules("").sources.autoMode);
    CHECK(sourcesToText(SourceSpec{}) == "auto");
}

TEST_CASE("library_sources: Alt-Schlüssel [general] library") {
    const LibraryRules sd = rules("[general]\nlibrary = sd\n");
    CHECK_FALSE(sd.sources.autoMode);
    CHECK(sd.sources.devices == Strs{"sd"});
    CHECK(rules("[general]\nlibrary = usb\n").sources.devices == Strs{"usb"});
    // [library] schlägt den Alt-Schlüssel.
    CHECK(rules("[general]\nlibrary = sd\n[library]\nsources = auto\n").sources.autoMode);
}

TEST_CASE("library_sources: unbekannte Geräte fallen weg, ganz leer heißt auto") {
    const LibraryRules r = rules("[library]\nsources = usb9, nas, usb, usb\n");
    CHECK(r.sources.devices == Strs{"usb"});
    CHECK(r.bad == Strs{"library.sources"});
    const LibraryRules none = rules("[library]\nsources = nas\n");
    CHECK(none.sources.autoMode);
}

TEST_CASE("library_sources: Standardordner und -endungen nach Spec 5.3") {
    const LibraryRules r = rules("");
    CHECK(paths(r, Console::Wii).folders == Strs{"wbfs", "wii"});
    CHECK(paths(r, Console::Wii).extensions == Strs{".wbfs", ".iso"});
    CHECK(paths(r, Console::Gc).folders == Strs{"games", "gc"});
    CHECK(paths(r, Console::Gc).extensions == Strs{".iso", ".gcm", ".ciso"});
    CHECK(paths(r, Console::N64).folders == Strs{"roms/n64"});
    CHECK(paths(r, Console::N64).extensions == Strs{".z64", ".n64", ".v64"});
    CHECK(paths(r, Console::Snes).folders == Strs{"roms/snes"});
    CHECK(paths(r, Console::Snes).extensions == Strs{".sfc", ".smc"});
    CHECK(paths(r, Console::Gba).extensions == Strs{".gba"});
}

TEST_CASE("library_sources: [paths] mit mehreren Ordnern") {
    const LibraryRules r = rules("[paths]\nwii = games/wii|wbfs\n");
    CHECK(paths(r, Console::Wii).folders == Strs{"games/wii", "wbfs"});
    CHECK(paths(r, Console::Gc).folders == Strs{"games", "gc"});
}

TEST_CASE("library_sources: ungültige Ordner fallen weg und werden gemeldet") {
    const LibraryRules r = rules("[paths]\nnes = ../x|/abs||ok|usb:/y|a/../b|roms\\snes/\n");
    CHECK(paths(r, Console::Nes).folders == Strs{"ok", "roms/snes"});
    CHECK(r.bad == Strs{"paths.nes"});
    // Nichts Gültiges übrig: Standard.
    const LibraryRules none = rules("[paths]\ngb = ..|/\n");
    CHECK(paths(none, Console::Gb).folders == Strs{"roms/gb"});
    CHECK(none.bad == Strs{"paths.gb"});
}

TEST_CASE("library_sources: höchstens 8 Ordner") {
    std::string line = "[paths]\nsnes = ";
    for (int i = 0; i < 50; ++i) line += "d" + std::to_string(i) + "|";
    const LibraryRules r = rules(line.c_str());
    REQUIRE(paths(r, Console::Snes).folders.size() == kMaxConsoleFolders);
    CHECK(paths(r, Console::Snes).folders[7] == "d7");
}

TEST_CASE("library_sources: [extensions] ersetzt die Liste") {
    const LibraryRules r = rules("[extensions]\ngba = .GBA|.zip\nnes = nes|.n/s\n");
    CHECK(paths(r, Console::Gba).extensions == Strs{".gba", ".zip"});
    CHECK(paths(r, Console::Nes).extensions == Strs{".nes"});  // nichts Gültiges: Standard
    CHECK(r.bad == Strs{"extensions.nes"});
}

TEST_CASE("library_sources: orderedSources") {
    const Strs mounted{"sd", "usb"};
    CHECK(orderedSources(SourceSpec{}, mounted) == Strs{"sd", "usb"});
    CHECK(orderedSources(SourceSpec{false, {"usb3"}}, mounted).empty());
    CHECK(orderedSources(SourceSpec{false, {"usb", "usb3", "sd"}}, mounted) == Strs{"usb", "sd"});
    const Strs withColon{"sd:", "USB:"};
    CHECK(orderedSources(SourceSpec{false, {"usb"}}, withColon) == Strs{"usb"});
}
