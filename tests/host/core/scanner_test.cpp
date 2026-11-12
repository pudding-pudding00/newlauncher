#include "core/bin_io.hpp"
#include "core/scanner.hpp"
#include "support/disc_images.hpp"
#include "support/mem_fs.hpp"
#include "support/zip_builder.hpp"
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
    fs.addFile("sd:/roms/nes/sub/tiefer/Tief.nes", {});  // Ebene 3
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

TEST_CASE("Meta-Titel einer bekannten Disc verfällt, wenn meta.bin ihn nicht mehr liefert") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/MKW [RMCP01]/RMCP01.wbfs", test::wbfsFile("RMCP01", "Mario Kart Wii"));
    const MetaDb meta = metaWith("RMCP01", "Mario Kart (Meta)");
    const ScanResult withMeta = scan(fs, nullptr, &meta);
    REQUIRE(byKey(withMeta.index, "RMCP01") != nullptr);
    REQUIRE(byKey(withMeta.index, "RMCP01")->title == "Mario Kart (Meta)");
    const ScanResult without = scan(fs, &withMeta.index);
    CHECK(byKey(without.index, "RMCP01")->title == "Mario Kart Wii");
    CHECK(without.stats.headersRead == 1);
    const ScanResult again = scan(fs, &without.index);
    CHECK(again.stats.headersRead == 0);  // Header-Titel bleibt ohne erneutes Lesen gültig
    CHECK(byKey(again.index, "RMCP01")->title == "Mario Kart Wii");
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
        return true;
    };
    REQUIRE(scanLibrary(fs, "sd:", nullptr, nullptr, cb, &spy).isOk());
    CHECK(spy.calls >= 5);
    CHECK(spy.rising);
    CHECK(spy.last >= 5);
}

TEST_CASE("Fortschritts-Callback mit false bricht den Scan ab") {
    test::MemFileSystem fs;
    for (int i = 0; i < 10; ++i) fs.addFile("sd:/roms/nes/" + std::to_string(i) + ".nes", {});
    fs.addFile("sd:/wbfs/MKW [RMCP01]/RMCP01.wbfs", test::wbfsFile("RMCP01", "Mario Kart Wii"));
    int calls = 0;
    auto cb = [](void* user, u32) {
        int& n = *static_cast<int*>(user);
        return ++n < 3;
    };
    auto r = scanLibrary(fs, "sd:", nullptr, nullptr, cb, &calls);
    CHECK_FALSE(r.isOk());
    if (!r.isOk()) CHECK(r.error().code == ErrorCode::Cancelled);
    CHECK(calls == 3);
    CHECK(fs.reads == 0);  // abgebrochen, bevor ein Disc-Header gelesen wurde
}

// ---- M12 Task 5: Ordnerregeln, Namensformen, FST, CISO, ZIP ----

namespace {
ScanResult scanWith(test::MemFileSystem& fs, const ScanRules& rules, const MetaDb* meta = nullptr) {
    auto r = scanLibrary(fs, "sd:", rules, nullptr, meta);
    REQUIRE(r.isOk());
    return std::move(r).value();
}
std::vector<u8> zipOf(std::initializer_list<const char*> names) {
    std::vector<test::TestZipEntry> e;
    for (const char* n : names) e.push_back(test::storedEntry(n, "x"));
    return test::buildZip(e);
}
}  // namespace

TEST_CASE("Wii-Namensformen: ID6 aus dem Kopf, Ersatztitel aus dem Namen nur ohne Kopftitel") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/Galaxy [RMGP01]/RMGP01.wbfs", test::wbfsFile("RMGP01", "Super Mario Galaxy"));
    fs.addFile("sd:/wbfs/RSBP01.wbfs", test::wbfsFile("RSBP01", "Smash Bros"));
    fs.addFile("sd:/wbfs/SOUP01_Zelda Skyward.iso", test::wiiIso("SOUP01", ""));
    fs.addFile("sd:/wbfs/Kart [RMCP01].wbfs", test::wbfsFile("RMCP01", ""));
    fs.addFile("sd:/wbfs/Sports.iso", test::wiiIso("RSPP01", ""));
    fs.addFile("sd:/wbfs/R8PP01.iso", test::wiiIso("R8PP01", ""));
    fs.addFile("sd:/wbfs/Falsch [ABCD01].iso", test::wiiIso("SMNP01", "New Super Mario"));
    fs.addFile("sd:/wbfs/Ordner [RZDP01]/RZDP01.iso", test::wiiIso("RZDP01", ""));
    const MetaDb meta = metaWith("SMNP01", "NSMB (Meta)");
    const ScanResult r = scan(fs, nullptr, &meta);
    REQUIRE(r.index.games.size() == 8);
    CHECK(byKey(r.index, "RMGP01")->title == "Super Mario Galaxy");
    CHECK(byKey(r.index, "RSBP01")->title == "Smash Bros");
    CHECK(byKey(r.index, "RSBP01")->path == "wbfs/RSBP01.wbfs");
    CHECK(byKey(r.index, "SOUP01")->title == "Zelda Skyward");
    CHECK(byKey(r.index, "RMCP01")->title == "Kart");
    CHECK(byKey(r.index, "RSPP01")->title == "Sports");
    CHECK(byKey(r.index, "R8PP01")->title == "R8PP01");
    CHECK(byKey(r.index, "SMNP01")->title == "NSMB (Meta)");  // ID6 aus dem Kopf, nicht aus dem Namen
    CHECK(byKey(r.index, "ABCD01") == nullptr);
    CHECK(byKey(r.index, "RZDP01")->title == "Ordner");
    CHECK((byKey(r.index, "RZDP01")->flags & kFlagTitleFromHeader) == 0);
}

TEST_CASE("Wii: .wbf1 neben einer Datei auf Ebene 1 setzt Split") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/Kart [RMCP01].wbfs", test::wbfsFile("RMCP01", "Mario Kart Wii"));
    fs.addFile("sd:/wbfs/Kart [RMCP01].wbf1", {});
    const ScanResult r = scan(fs);
    REQUIRE(r.index.games.size() == 1);
    CHECK((byKey(r.index, "RMCP01")->flags & kFlagSplitFile) != 0);
}

TEST_CASE("GC: FST-Ordner, Namensformen, .gcm und CISO") {
    test::MemFileSystem fs;
    fs.addFile("sd:/games/Zelda [GZLE01]/sys/boot.bin", test::gcIso("GZLE01", "Wind Waker"));
    fs.addDir("sd:/games/Zelda [GZLE01]/files");
    fs.addFile("sd:/games/Melee [GALP01].iso", test::gcIso("GALP01", "Melee"));
    fs.addFile("sd:/games/GM4P01.gcm", test::gcIso("GM4P01", "Mario Kart DD"));
    fs.addFile("sd:/gc/Pikmin [GPIP01].ciso", test::cisoFile("GPIP01", "Pikmin"));
    fs.addFile("sd:/gc/Sunshine/game.nkit.iso", test::gcIso("GMSP01", "Sunshine"));
    const ScanResult r = scan(fs);
    REQUIRE(r.index.games.size() == 5);
    const IndexGame* z = byKey(r.index, "GZLE01");
    REQUIRE(z != nullptr);
    CHECK(z->path == "games/Zelda [GZLE01]");
    CHECK(z->title == "Wind Waker");
    CHECK(byKey(r.index, "GALP01")->path == "games/Melee [GALP01].iso");
    CHECK(byKey(r.index, "GM4P01")->console == Console::Gc);
    CHECK(byKey(r.index, "GPIP01")->title == "Pikmin");  // Kopf bei 0x8000
    CHECK(byKey(r.index, "GMSP01")->path == "gc/Sunshine/game.nkit.iso");
}

TEST_CASE("Tiefe: Ebene 2 gefunden, Ebene 3 nicht; ROMs ebenso") {
    test::MemFileSystem fs;
    fs.addFile("sd:/games/sub/x.iso", test::gcIso("GAAP01", "Zwei"));
    fs.addFile("sd:/games/sub/sub2/y.iso", test::gcIso("GBBP01", "Drei"));
    fs.addFile("sd:/roms/gba/Sammlung/Metroid.gba", {});
    fs.addFile("sd:/roms/gba/Sammlung/Tiefer/Zu tief.gba", {});
    const ScanResult r = scan(fs);
    CHECK(byKey(r.index, "GAAP01") != nullptr);
    CHECK(byKey(r.index, "GBBP01") == nullptr);
    const IndexGame* m = byKey(r.index, "gba/Metroid.gba");
    REQUIRE(m != nullptr);
    CHECK(m->path == "roms/gba/Sammlung/Metroid.gba");
    CHECK(byKey(r.index, "gba/Zu tief.gba") == nullptr);
}

TEST_CASE("ZIP-ROMs: passender Eintrag nötig, Konsole muss ZIP annehmen") {
    test::MemFileSystem fs;
    fs.addFile("sd:/roms/nes/Zelda (Europe).zip", zipOf({"Zelda (Europe).nes"}));
    fs.addFile("sd:/roms/nes/Handbuch.zip", zipOf({"liesmich.txt"}));
    fs.addFile("sd:/roms/nes/Kaputt.zip", {1, 2, 3});
    ScanRules rules = defaultScanRules();
    rules.paths[static_cast<std::size_t>(Console::Nes)].extensions = {".nes", ".zip"};
    const ScanResult r = scanWith(fs, rules);
    const IndexGame* z = byKey(r.index, "nes/Zelda (Europe).zip");
    REQUIRE(z != nullptr);
    CHECK(z->title == "Zelda");
    CHECK(z->path == "roms/nes/Zelda (Europe).zip");
    CHECK(byKey(r.index, "nes/Handbuch.zip") == nullptr);
    CHECK(byKey(r.index, "nes/Kaputt.zip") == nullptr);

    rules.zipConsoles = 0;
    CHECK(scanWith(fs, rules).index.games.empty());
}

TEST_CASE("zwei Ordner für eine Konsole: beide gescannt, gleicher Schlüssel einmal") {
    test::MemFileSystem fs;
    fs.addFile("sd:/a/Zelda.nes", {});
    fs.addFile("sd:/b/Zelda.nes", {});
    fs.addFile("sd:/b/Metroid.nes", {});
    ScanRules rules = defaultScanRules();
    rules.paths[static_cast<std::size_t>(Console::Nes)].folders = {"a", "b"};
    const ScanResult r = scanWith(fs, rules);
    REQUIRE(r.index.games.size() == 2);
    CHECK(byKey(r.index, "nes/Zelda.nes")->path == "a/Zelda.nes");
    CHECK(byKey(r.index, "nes/Metroid.nes") != nullptr);
}

TEST_CASE("maxDepth 1 sucht nur direkt im Ordner") {
    test::MemFileSystem fs;
    fs.addFile("sd:/wbfs/RSBP01.wbfs", test::wbfsFile("RSBP01", "Smash"));
    fs.addFile("sd:/wbfs/Galaxy [RMGP01]/RMGP01.wbfs", test::wbfsFile("RMGP01", "Galaxy"));
    ScanRules rules = defaultScanRules();
    rules.maxDepth = 1;
    const ScanResult r = scanWith(fs, rules);
    CHECK(byKey(r.index, "RSBP01") != nullptr);
    CHECK(byKey(r.index, "RMGP01") == nullptr);
}
