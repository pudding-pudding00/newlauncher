#include "core/bin_io.hpp"
#include "core/index_file.hpp"
#include "core/meta_db.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

namespace {
IndexData sample() {
    IndexData d;
    d.folders.push_back(IndexFolder{"roms/nes", 0x01020304});
    d.games.push_back(IndexGame{Console::Nes, 0, "nes/A.nes", "A", "roms/nes/A.nes", 16});
    return d;
}

std::vector<u8> bytes(std::initializer_list<int> v) {
    std::vector<u8> out;
    for (int b : v) out.push_back(static_cast<u8>(b));
    return out;
}

void appendStr(std::vector<u8>& v, std::string_view s) {
    v.push_back(static_cast<u8>(s.size() >> 8));
    v.push_back(static_cast<u8>(s.size() & 0xFF));
    v.insert(v.end(), s.begin(), s.end());
}
}  // namespace

TEST_CASE("writeIndex erzeugt exakt die erwarteten Bytes") {
    std::vector<u8> expected = bytes({'N', 'L', 'I', 'X', 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1});
    appendStr(expected, "roms/nes");
    for (int b : {1, 2, 3, 4}) expected.push_back(static_cast<u8>(b));
    expected.push_back(0);  // NES
    expected.push_back(0);  // flags
    appendStr(expected, "nes/A.nes");
    appendStr(expected, "A");
    appendStr(expected, "roms/nes/A.nes");
    for (int b : {0, 0, 0, 0, 0, 0, 0, 16}) expected.push_back(static_cast<u8>(b));
    CHECK(writeIndex(sample()) == expected);
}

TEST_CASE("readIndex(writeIndex(x)) == x mit Umlauten und 300 Spielen") {
    IndexData d;
    d.folders.push_back(IndexFolder{"wbfs/Pokémon", 7});
    for (int i = 0; i < 300; ++i) {
        d.games.push_back(IndexGame{static_cast<Console>(i % 8), static_cast<u8>(i % 8), "k" + std::to_string(i),
                                    "Titel Ä " + std::to_string(i), "p/" + std::to_string(i), 0x100000000ull + i});
    }
    auto r = readIndex(writeIndex(d));
    REQUIRE(r.isOk());
    const IndexData& b = r.value();
    REQUIRE(b.games.size() == 300);
    CHECK(b.folders[0].path == "wbfs/Pokémon");
    CHECK(b.folders[0].mtime == 7);
    CHECK(b.games[299].console == static_cast<Console>(299 % 8));
    CHECK(b.games[299].flags == 299 % 8);
    CHECK(b.games[299].title == "Titel Ä 299");
    CHECK(b.games[299].size == 0x100000000ull + 299);
}

TEST_CASE("readIndex verwirft falsches Magic, falsche Version, abgeschnittene Datei, zu große Stringlänge, Konsole 9") {
    const std::vector<u8> good = writeIndex(sample());
    for (std::size_t n = 0; n < good.size(); ++n) {
        CHECK_FALSE(readIndex(std::span<const u8>(good.data(), n)).isOk());
    }
    std::vector<u8> bad = good;
    bad[0] = 'X';
    CHECK_FALSE(readIndex(bad).isOk());
    bad = good;
    bad[5] = 2;  // Version 2
    CHECK_FALSE(readIndex(bad).isOk());
    bad = good;
    bad[16] = 0xFF;  // Länge des Ordnerpfads riesig
    CHECK_FALSE(readIndex(bad).isOk());
    bad = good;
    bad[16 + 2 + 8 + 4] = 9;  // Konsole 9
    CHECK_FALSE(readIndex(bad).isOk());
    bad = good;
    bad[11] = 0xFF;  // Ordneranzahl > Rest der Datei
    CHECK_FALSE(readIndex(bad).isOk());
    bad = good;
    bad.push_back(0);  // Müll am Ende
    CHECK_FALSE(readIndex(bad).isOk());
}

TEST_CASE("BeReader bleibt nach dem ersten Fehler im Fehlerzustand") {
    const std::vector<u8> data = bytes({0, 1, 2});
    BeReader r(data);
    u32 v = 0;
    CHECK_FALSE(r.u32(v));
    CHECK_FALSE(r.ok());
    u8 b = 0;
    CHECK_FALSE(r.u8(b));
    std::string s;
    CHECK_FALSE(r.str(s));
}

TEST_CASE("BeWriter kürzt überlange Strings auf eine Codepoint-Grenze") {
    std::string s(70000, 'a');
    s[65534] = '\xC3';
    s[65535] = '\xA4';  // ä über die Grenze
    BeWriter w;
    w.str(s);
    const auto& out = w.bytes();
    const std::size_t len = static_cast<std::size_t>(out[0]) << 8 | out[1];
    CHECK(len == 65534);
}

namespace {
std::vector<u8> metaFile(std::initializer_list<std::string_view> keys) {
    BeWriter w;
    w.u8('N'); w.u8('L'); w.u8('M'); w.u8('D');
    w.u16(1);
    w.u16(0);
    w.u32(static_cast<u32>(keys.size()));
    int i = 0;
    for (std::string_view k : keys) {
        w.str(k);
        w.str(std::string("Titel ") + std::string(k));
        w.u16(static_cast<u16>(1990 + i));
        w.u8(static_cast<u8>(i + 1));
        w.str("Beschreibung");
        ++i;
    }
    return w.bytes();
}
}  // namespace

TEST_CASE("MetaDb findet Einträge per binärer Suche, unbekannter Schlüssel → nullopt") {
    auto db = MetaDb::parse(metaFile({"RMCP01", "n64/a", "n64/b", "n64/\xC3\xA4"}));
    REQUIRE(db.isOk());
    CHECK(db.value().size() == 4);
    auto e = db.value().find("n64/b");
    REQUIRE(e.has_value());
    CHECK(e->title == "Titel n64/b");
    CHECK(e->year == 1992);
    CHECK(e->players == 3);
    CHECK(e->description == "Beschreibung");
    CHECK(db.value().find("RMCP01").has_value());
    CHECK(db.value().find("n64/\xC3\xA4").has_value());
    CHECK_FALSE(db.value().find("n64/c").has_value());
    CHECK_FALSE(db.value().find("").has_value());
}

TEST_CASE("MetaDb verwirft unsortierte Schlüssel und abgeschnittene Dateien") {
    CHECK_FALSE(MetaDb::parse(metaFile({"b", "a"})).isOk());
    CHECK_FALSE(MetaDb::parse(metaFile({"a", "a"})).isOk());
    const std::vector<u8> good = metaFile({"a", "b"});
    for (std::size_t n = 0; n < good.size(); ++n) {
        CHECK_FALSE(MetaDb::parse(std::vector<u8>(good.begin(), good.begin() + static_cast<long>(n))).isOk());
    }
    CHECK(MetaDb::parse(metaFile({})).isOk());
}

TEST_CASE("metaKey") {
    CHECK(metaKey(IndexGame{Console::Wii, 0, "RMCP01", "", "", 0}) == "RMCP01");
    CHECK(metaKey(IndexGame{Console::N64, 0, "n64/Super Mario 64 (Europe).z64", "", "", 0}) ==
          "n64/super mario 64 (europe)");
    CHECK(metaKey(IndexGame{Console::Nes, 0, "nes/OhneEndung", "", "", 0}) == "nes/ohneendung");
    CHECK(metaKey(IndexGame{Console::Gb, 0, "gb/Ärger.gb", "", "", 0}) == "gb/\xC3\x84rger");  // nur ASCII wird klein
}
