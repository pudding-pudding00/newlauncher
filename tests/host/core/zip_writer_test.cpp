#include "core/zip_writer.hpp"
#include "core/zip_reader.hpp"
#include <doctest/doctest.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace nl;

namespace {
std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

std::vector<u8> sampleZip() {
    ZipWriter w;
    const u32 t = dosTimeFromUnix(1764115200);
    std::string text;
    for (int i = 0; i < 100; ++i) text += "Spielstand ";
    CHECK(w.add("newlauncher-save.ini", bytes("[save]\nformat = 1\n"), false, t).isOk());
    CHECK(w.add("a/b.bin", bytes(text), true, t).isOk());
    CHECK(w.add("c.raw", std::vector<u8>(5000, 0), true, t).isOk());
    return w.finish();
}
}  // namespace

TEST_CASE("dosTimeFromUnix: 2025-11-26 00:00:00 UTC") {
    CHECK(dosTimeFromUnix(1764115200) == (u32{((2025 - 1980) << 9) | (11 << 5) | 26} << 16));
    CHECK(dosTimeFromUnix(1764115200 + 13 * 3600 + 7 * 60 + 9) ==
          ((u32{((2025 - 1980) << 9) | (11 << 5) | 26} << 16) | (13u << 11) | (7u << 5) | 4u));
}

TEST_CASE("ZipWriter: Rundlauf über das Lesen aus dem Speicher") {
    const std::vector<u8> zip = sampleZip();
    auto dir = readZipDirectory(std::span<const u8>(zip));
    REQUIRE(dir.isOk());
    REQUIRE(dir.value().size() == 3);
    CHECK(dir.value()[0].name == "newlauncher-save.ini");
    CHECK(dir.value()[0].method == 0);
    CHECK(dir.value()[1].name == "a/b.bin");
    CHECK(dir.value()[1].method == 8);
    CHECK(dir.value()[2].compSize < 200);
    auto b = extractZipEntry(std::span<const u8>(zip), dir.value()[1]);
    REQUIRE(b.isOk());
    CHECK(b.value().size() == 1100);
    auto c = extractZipEntry(std::span<const u8>(zip), dir.value()[2]);
    REQUIRE(c.isOk());
    CHECK(c.value() == std::vector<u8>(5000, 0));
}

TEST_CASE("ZipWriter: Deflate nur, wenn es kleiner wird") {
    ZipWriter w;
    CHECK(w.add("x", bytes("ab"), true, 0).isOk());
    const std::vector<u8> zip = w.finish();
    auto dir = readZipDirectory(std::span<const u8>(zip));
    REQUIRE(dir.isOk());
    CHECK(dir.value()[0].method == 0);
}

TEST_CASE("ZipWriter: unsichere und doppelte Namen") {
    ZipWriter w;
    CHECK(!w.add("../x", bytes("1"), false, 0).isOk());
    CHECK(!w.add("a//b", bytes("1"), false, 0).isOk());
    CHECK(!w.add("/abs", bytes("1"), false, 0).isOk());
    CHECK(w.add("a/b", bytes("1"), false, 0).isOk());
    CHECK(!w.add("a/b", bytes("2"), false, 0).isOk());
}

TEST_CASE("zip aus dem Speicher: abgeschnittenes Archiv") {
    std::vector<u8> zip = sampleZip();
    zip.resize(zip.size() - 10);
    CHECK(!readZipDirectory(std::span<const u8>(zip)).isOk());
}

TEST_CASE("ZipWriter: Beispiel für die Python-Prüfung schreiben") {
    const std::vector<u8> zip = sampleZip();
    if (FILE* f = std::fopen("zip_writer_sample.zip", "wb")) {  // Arbeitsordner = build/host (ctest)
        std::fwrite(zip.data(), 1, zip.size(), f);
        std::fclose(f);
    }
}
