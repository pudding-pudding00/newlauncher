#include "core/zip_reader.hpp"
#include "support/deflate_samples.hpp"
#include "support/mem_fs.hpp"
#include "support/zip_builder.hpp"
#include <doctest/doctest.h>
using namespace nl;
using namespace nl::test;

namespace {
constexpr const char* kZip = "sd:/newlauncher/tmp/x.zip";

std::span<const u8> dynamicPacked() { return {kDynamicData, sizeof kDynamicData}; }

std::vector<TestZipEntry> goodEntries() {
    return {dirEntry("apps/x/"), storedEntry("apps/x/meta.xml", "<app/>"),
            deflatedEntry("apps/x/boot.dol", dynamicPacked(), kDynamicSize, kDynamicCrc)};
}

Result<std::vector<ZipEntry>> dirOf(MemFileSystem& fs, const std::vector<u8>& zip) {
    fs.addFile(kZip, zip);
    return readZipDirectory(fs, kZip, zip.size());
}
}  // namespace

TEST_CASE("zip: Verzeichnis lesen und Einträge entpacken") {
    MemFileSystem fs;
    auto dir = dirOf(fs, buildZip(goodEntries()));
    REQUIRE(dir.isOk());
    const auto& e = dir.value();
    REQUIRE(e.size() == 3);
    CHECK(e[0].name == "apps/x/");
    CHECK(e[0].isDir);
    CHECK(e[1].name == "apps/x/meta.xml");
    CHECK(!e[1].isDir);
    CHECK(e[1].size == 6);
    CHECK(e[2].method == 8);
    CHECK(e[2].size == kDynamicSize);

    auto meta = extractZipEntry(fs, kZip, e[1]);
    REQUIRE(meta.isOk());
    CHECK(std::string(meta.value().begin(), meta.value().end()) == "<app/>");
    auto dol = extractZipEntry(fs, kZip, e[2]);
    REQUIRE(dol.isOk());
    CHECK(dol.value().size() == kDynamicSize);
    CHECK(crc32(dol.value()) == kDynamicCrc);
}

TEST_CASE("zip: Archivkommentar hinter dem Verzeichnisende") {
    MemFileSystem fs;
    auto dir = dirOf(fs, buildZip(goodEntries(), "Kommentar mit PK\x05\x06 darin"));
    REQUIRE(dir.isOk());
    CHECK(dir.value().size() == 3);
}

TEST_CASE("zip: kein Verzeichnisende") {
    MemFileSystem fs;
    std::vector<u8> junk(300, 0x41);
    auto dir = dirOf(fs, junk);
    REQUIRE(!dir.isOk());
    CHECK(dir.error().code == ErrorCode::InvalidData);
    CHECK(!dirOf(fs, {}).isOk());
}

TEST_CASE("zip: zu viele Einträge") {
    MemFileSystem fs;
    std::vector<TestZipEntry> many;
    for (u32 i = 0; i <= kMaxZipEntries; ++i) many.push_back(dirEntry("d" + std::to_string(i) + "/"));
    CHECK(!dirOf(fs, buildZip(many)).isOk());
}

TEST_CASE("zip: unbekannte Methode, verschlüsselt, zu groß") {
    MemFileSystem fs;
    auto e = storedEntry("a.txt", "x");
    e.method = 12;
    CHECK(!dirOf(fs, buildZip({e})).isOk());
    e = storedEntry("a.txt", "x");
    e.flags = 1;
    CHECK(!dirOf(fs, buildZip({e})).isOk());
    e = storedEntry("a.txt", "x");
    e.size = kMaxZipEntryBytes + 1;
    CHECK(!dirOf(fs, buildZip({e})).isOk());
}

TEST_CASE("zip: unsicherer Name lehnt das ganze Archiv ab") {
    MemFileSystem fs;
    auto entries = goodEntries();
    entries.push_back(storedEntry("../evil.dol", "x"));
    auto dir = dirOf(fs, buildZip(entries));
    REQUIRE(!dir.isOk());
    CHECK(dir.error().code == ErrorCode::InvalidData);
    CHECK(dir.error().context.find("../evil.dol") != std::string::npos);
}

TEST_CASE("zip: falsche CRC und falsche Größe beim Entpacken") {
    MemFileSystem fs;
    auto bad = storedEntry("a.txt", "hallo");
    bad.crc ^= 1;
    auto dir = dirOf(fs, buildZip({bad}));
    REQUIRE(dir.isOk());
    CHECK(!extractZipEntry(fs, kZip, dir.value()[0]).isOk());

    auto lie = deflatedEntry("b.dol", dynamicPacked(), kDynamicSize - 5, kDynamicCrc);
    MemFileSystem fs2;
    auto dir2 = dirOf(fs2, buildZip({lie}));
    REQUIRE(dir2.isOk());
    CHECK(!extractZipEntry(fs2, kZip, dir2.value()[0]).isOk());
}

TEST_CASE("zip: lokaler Kopf kaputt oder Daten hinter dem Dateiende") {
    MemFileSystem fs;
    auto zip = buildZip({storedEntry("a.txt", "hallo")});
    auto dir = dirOf(fs, zip);
    REQUIRE(dir.isOk());
    ZipEntry e = dir.value()[0];
    zip[0] = 'X';  // Signatur des lokalen Kopfs
    fs.addFile(kZip, zip);
    CHECK(!extractZipEntry(fs, kZip, e).isOk());

    MemFileSystem fs2;
    auto dir2 = dirOf(fs2, buildZip({storedEntry("a.txt", "hallo")}));
    REQUIRE(dir2.isOk());
    ZipEntry far = dir2.value()[0];
    far.compSize = 100000;
    CHECK(!extractZipEntry(fs2, kZip, far).isOk());
}

TEST_CASE("isSafeZipPath") {
    CHECK(isSafeZipPath("apps/a/boot.dol"));
    CHECK(isSafeZipPath("apps/a/"));
    CHECK(isSafeZipPath("fceugx/roms/x..y.nes"));
    CHECK(!isSafeZipPath(""));
    CHECK(!isSafeZipPath("../x"));
    CHECK(!isSafeZipPath("a/../../x"));
    CHECK(!isSafeZipPath("a/.."));
    CHECK(!isSafeZipPath("/x"));
    CHECK(!isSafeZipPath("C:/x"));
    CHECK(!isSafeZipPath("a\\b"));
    CHECK(!isSafeZipPath("a//b"));
}
