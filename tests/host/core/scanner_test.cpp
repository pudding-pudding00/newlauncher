#include "core/bin_io.hpp"
#include "core/scanner.hpp"
#include "support/disc_images.hpp"
#include "support/mem_fs.hpp"
#include <doctest/doctest.h>
#include <string>
using namespace nl;

namespace {
const IndexGame* byKey(const IndexData& d, std::string_view key) {
    for (const IndexGame& g : d.games)
        if (g.key == key) return &g;
    return nullptr;
}

ScanResult scan(test::MemFileSystem& fs, const IndexData* prev = nullptr, const MetaDb* meta = nullptr) {
    auto r = scanLibrary(fs, "sd:", prev, meta);
    REQUIRE(r.isOk());
    return std::move(r).value();
}

MetaDb metaWith(std::string_view key, std::string_view title) {
    BeWriter w;
    for (char c : std::string_view("NLMD")) w.u8(static_cast<u8>(c));
    w.u16(1);
    w.u16(0);
    w.u32(1);
    w.str(key);
    w.str(title);
    w.u16(1996);
    w.u8(1);
    w.str("Beschreibung");
    auto db = MetaDb::parse(w.bytes());
    REQUIRE(db.isOk());
    return std::move(db).value();
}
}  // namespace

TEST_CASE("ROMs: Endungen, Groß/klein, nicht rekursiv, readme.txt ignoriert") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe).nes", {});
    fs.addFile("sd:/roms/snes/F-Zero (Europe).SMC", {1});
    fs.addFile("sd:/roms/nes/readme.txt", {});
    fs.addFile("sd:/roms/nes/sub/Tief.nes", {});
    fs.addFile("sd:/roms/n64/Mario.v64", {});
    const ScanResult r = scan(fs);
    CHECK(r.index.games.size() == 3);
    const IndexGame* z = byKey(r.index, "nes/Zelda (Europe).nes");
    REQUIRE(z != nullptr);
    CHECK(z->path == "roms/nes/Zelda (Europe).nes");
    CHECK(byKey(r.index, "snes/F-Zero (Europe).SMC") != nullptr);
    CHECK(byKey(r.index, "n64/Mario.v64") != nullptr);
    CHECK(byKey(r.index, "nes/Tief.nes") == nullptr);
}

TEST_CASE("ROM-Titel aus Dateiname bereinigt, Meta-Titel hat Vorrang") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/n64/Super Mario 64 (Europe) (En,Fr,De).z64", {});
    fs.addFile("sd:/roms/n64/Mario Kart 64 (Europe).z64", {});
    const MetaDb meta = metaWith("n64/super mario 64 (europe) (en,fr,de)", "Super Mario 64 (Meta)");
    const ScanResult r = scan(fs, nullptr, &meta);
    CHECK(byKey(r.index, "n64/Super Mario 64 (Europe) (En,Fr,De).z64")->title == "Super Mario 64 (Meta)");
    CHECK(byKey(r.index, "n64/Mario Kart 64 (Europe).z64")->title == "Mario Kart 64");
}

TEST_CASE("GC: game.iso mit Header, disc2.iso setzt Flag, Ordner ohne game.iso wird ignoriert") {
    test::MemFileSystem fs;
    fs.addFile("sd:/games/Metroid Prime [GM8P01]/game.iso", test::gcIso("GM8P01", "Metroid Prime"));
    fs.addFile("sd:/games/Tales [GTOP01]/game.iso", test::gcIso("GTOP01", "Tales"));
    fs.addFile("sd:/games/Tales [GTOP01]/disc2.iso", test::gcIso("GTOP01", "Tales"));
    fs.addFile("sd:/games/Leer/info.txt", {});
    const ScanResult r = scan(fs);
    REQUIRE(r.index.games.size() == 2);
    const IndexGame* m = byKey(r.index, "GM8P01");
    REQUIRE(m != nullptr);
    CHECK(m->console == Console::Gc);
    CHECK(m->title == "Metroid Prime");
    CHECK((m->flags & kFlagTitleFromHeader) != 0);
    CHECK(m->path == "games/Metroid Prime [GM8P01]/game.iso");
    CHECK((byKey(r.index, "GTOP01")->flags & kFlagSecondDisc) != 0);
}

TEST_CASE("Wii: .wbfs und .iso, .wbf1 setzt Split-Flag, doppelte ID6 einmal") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbfs", test::wbfsFile("RMCP01", "Mario Kart Wii"));
    fs.addFile("sd:/wbfs/Mario Kart Wii [RMCP01]/RMCP01.wbf1", {});
    fs.addFile("sd:/wbfs/Galaxy [RMGP01]/RMGP01.iso", test::wiiIso("RMGP01", "Super Mario Galaxy"));
    fs.addFile("sd:/wbfs/Zweitkopie [RMGP01]/RMGP01.wbfs", test::wbfsFile("RMGP01", "Doppelt"));
    const ScanResult r = scan(fs);
    REQUIRE(r.index.games.size() == 2);
    CHECK((byKey(r.index, "RMCP01")->flags & kFlagSplitFile) != 0);
    CHECK(byKey(r.index, "RMGP01")->title == "Super Mario Galaxy");  // erste Datei nach Ordnername
    CHECK(r.stats.skipped == 1);
}

TEST_CASE("kaputte Header und abgeschnittene Dateien werden übersprungen und gezählt") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/Kaputt [XXXX01]/XXXX01.wbfs", std::vector<u8>(1024, 0xAB));
    fs.addFile("sd:/wbfs/Kurz [RSBP01]/RSBP01.iso", std::vector<u8>(100, 0));
    fs.addFile("sd:/games/Leer [GALP01]/game.iso", {});
    fs.addFile("sd:/roms/gb/Tetris.gb", {});
    const ScanResult r = scan(fs);
    CHECK(r.index.games.size() == 1);
    CHECK(r.stats.skipped == 3);
}

TEST_CASE("bekannte Datei (Pfad+Größe) wird ohne Header-Lesen übernommen; geänderte Größe liest neu") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/MKW [RMCP01]/RMCP01.wbfs", test::wbfsFile("RMCP01", "Mario Kart Wii"));
    const ScanResult first = scan(fs);
    CHECK(first.stats.headersRead == 1);
    const ScanResult second = scan(fs, &first.index);
    CHECK(second.stats.headersRead == 0);
    CHECK(byKey(second.index, "RMCP01")->title == "Mario Kart Wii");
    fs.addFile("sd:/wbfs/MKW [RMCP01]/RMCP01.wbfs", test::wbfsFile("RMCP01", "Neu", 9, 2048));
    const ScanResult third = scan(fs, &second.index);
    CHECK(third.stats.headersRead == 1);
    CHECK(byKey(third.index, "RMCP01")->title == "Neu");
}

TEST_CASE("added/removed gegenüber previous") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/A.nes", {});
    fs.addFile("sd:/roms/nes/B.nes", {});
    const ScanResult first = scan(fs);
    CHECK(first.stats.added == 2);
    fs.remove("sd:/roms/nes/A.nes");
    fs.addFile("sd:/roms/nes/C.nes", {});
    const ScanResult second = scan(fs, &first.index);
    CHECK(second.stats.added == 1);
    CHECK(second.stats.removed == 1);
}

TEST_CASE("Sortierung: Konsole, dann Titel ohne Groß/klein") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/snes/b.sfc", {});
    fs.addFile("sd:/roms/nes/zelda.nes", {});
    fs.addFile("sd:/roms/nes/Banjo.nes", {});
    fs.addFile("sd:/roms/nes/apple.nes", {});
    const ScanResult r = scan(fs);
    REQUIRE(r.index.games.size() == 4);
    CHECK(r.index.games[0].title == "apple");
    CHECK(r.index.games[1].title == "Banjo");
    CHECK(r.index.games[2].title == "zelda");
    CHECK(r.index.games[3].console == Console::Snes);
}

TEST_CASE("fehlende Konsolenordner sind kein Fehler; nicht lesbares Wurzelverzeichnis ist einer") {
    test::MemFileSystem fs;
    fs.addDir("sd:", 0);
    const ScanResult r = scan(fs);
    CHECK(r.index.games.empty());
    test::MemFileSystem empty;
    CHECK_FALSE(scanLibrary(empty, "usb:", nullptr, nullptr).isOk());
}

TEST_CASE("Ordner werden mit mtime in den Index geschrieben") {
    test::MemFileSystem fs;
    fs.addDir("sd:/roms/nes", 42);
    fs.addFile("sd:/roms/nes/A.nes", {});
    const ScanResult r = scan(fs);
    bool found = false;
    for (const IndexFolder& f : r.index.folders)
        if (f.path == "roms/nes" && f.mtime == 42) found = true;
    CHECK(found);
}

TEST_CASE("Fortschritts-Callback wird mit steigender Anzahl aufgerufen") {
    test::MemFileSystem fs;
    for (int i = 0; i < 5; ++i) fs.addFile("sd:/roms/nes/" + std::to_string(i) + ".nes", {});
    struct Spy { u32 last = 0; bool rising = true; int calls = 0; } spy;
    auto cb = [](void* user, u32 seen) {
        auto* s = static_cast<Spy*>(user);
        if (seen < s->last) s->rising = false;
        s->last = seen;
        ++s->calls;
    };
    REQUIRE(scanLibrary(fs, "sd:", nullptr, nullptr, cb, &spy).isOk());
    CHECK(spy.calls >= 5);
    CHECK(spy.rising);
    CHECK(spy.last >= 5);
}
