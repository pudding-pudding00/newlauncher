#include "core/save_archive.hpp"
#include "core/zip_reader.hpp"
#include "core/zip_writer.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

namespace {
std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

std::vector<SaveFile> sample() {
    return {SaveFile{"banner.bin", bytes("BANNER")}, SaveFile{"save/slot1.dat", std::vector<u8>(3000, 7)},
            SaveFile{"a.txt", bytes("hallo")}};
}

SaveMeta meta() {
    SaveMeta m;
    m.unit = "wii-RMGP";
    m.title = "Super Mario Galaxy – Grüße";
    m.device = "Wii Wohnzimmer";
    m.saved = 1764115200;
    return m;
}
}  // namespace

TEST_CASE("contentHash: unabhängig von der Reihenfolge") {
    std::vector<SaveFile> a = sample();
    std::vector<SaveFile> b{a[2], a[0], a[1]};
    CHECK(contentHash(a) == contentHash(b));
    CHECK(contentHash(a).size() == 64);
}

TEST_CASE("contentHash: ein Byte ändert den Hash") {
    std::vector<SaveFile> a = sample();
    std::vector<SaveFile> b = a;
    b[1].data[100] = 8;
    CHECK(contentHash(a) != contentHash(b));
}

TEST_CASE("contentHash: leer ist SHA-256 von nichts") {
    CHECK(contentHash({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("packSave/unpackSave: Rundlauf mit Meta") {
    auto z = packSave(meta(), sample());
    CHECK(z.isOk());
    if (!z.isOk()) return;
    auto p = unpackSave(z.value());
    CHECK(p.isOk());
    if (!p.isOk()) return;
    const SavePackage& pkg = p.value();
    CHECK(pkg.meta.format == 1);
    CHECK(pkg.meta.unit == "wii-RMGP");
    CHECK(pkg.meta.title == "Super Mario Galaxy – Grüße");
    CHECK(pkg.meta.device == "Wii Wohnzimmer");
    CHECK(pkg.meta.saved == 1764115200);
    CHECK(pkg.meta.files == 3);
    CHECK(pkg.meta.hash == contentHash(sample()));
    CHECK(pkg.files.size() == 3);
    CHECK(contentHash(pkg.files) == pkg.meta.hash);
    auto m = readSaveMeta(z.value());
    CHECK(m.isOk());
    if (m.isOk()) CHECK(m.value().hash == pkg.meta.hash);
}

TEST_CASE("packSave: Zeilenumbruch im Titel bricht die Meta nicht") {
    SaveMeta m = meta();
    m.title = "Zeile1\nhash = 00";
    auto z = packSave(m, sample());
    CHECK(z.isOk());
    if (!z.isOk()) return;
    auto p = unpackSave(z.value());
    CHECK(p.isOk());
    if (p.isOk()) CHECK(p.value().meta.hash == contentHash(sample()));
}

TEST_CASE("unpackSave: manipulierte Datei") {
    // Paket mit gültiger Meta, aber anderem Inhalt als der Hash sagt.
    auto good = packSave(meta(), sample());
    CHECK(good.isOk());
    if (!good.isOk()) return;
    auto metaEntry = readZipDirectory(std::span<const u8>(good.value()));
    CHECK(metaEntry.isOk());
    if (!metaEntry.isOk()) return;
    auto metaBytes = extractZipEntry(std::span<const u8>(good.value()), metaEntry.value()[0]);
    CHECK(metaBytes.isOk());
    if (!metaBytes.isOk()) return;
    ZipWriter w;
    CHECK(w.add(kSaveMetaName, metaBytes.value(), false, 0).isOk());
    std::vector<SaveFile> evil = sample();
    evil[0].data[0] = 'X';
    for (const SaveFile& f : evil) CHECK(w.add(f.path, f.data, true, 0).isOk());
    const std::vector<u8> zip = w.finish();
    auto p = unpackSave(zip);
    CHECK(!p.isOk());
    if (!p.isOk()) CHECK(p.error().code == ErrorCode::InvalidData);
}

TEST_CASE("unpackSave: Meta fehlt oder steht nicht vorn") {
    ZipWriter w;
    CHECK(w.add("a.txt", bytes("x"), false, 0).isOk());
    CHECK(w.add(kSaveMetaName, bytes("[save]\nformat = 1\n"), false, 0).isOk());
    const std::vector<u8> zip = w.finish();
    CHECK(!unpackSave(zip).isOk());
}

TEST_CASE("unpackSave: Format 2 ist VersionMismatch") {
    ZipWriter w;
    CHECK(w.add(kSaveMetaName, bytes("[save]\nformat = 2\nunit = x\nhash = 00\n"), false, 0).isOk());
    const std::vector<u8> zip = w.finish();
    auto p = unpackSave(zip);
    CHECK(!p.isOk());
    if (!p.isOk()) CHECK(p.error().code == ErrorCode::VersionMismatch);
}

TEST_CASE("packSave: mehr als 16 MiB") {
    std::vector<SaveFile> big{SaveFile{"a", std::vector<u8>(8u << 20, 0)}, SaveFile{"b", std::vector<u8>((8u << 20) + 1, 0)}};
    CHECK(!packSave(meta(), big).isOk());
}

TEST_CASE("unpackSave: Grenze aus dem Verzeichnis, ohne zu entpacken") {
    auto z = packSave(meta(), sample());
    CHECK(z.isOk());
    if (!z.isOk()) return;
    CHECK(!unpackSave(z.value(), 100).isOk());  // 3011 Bytes roh > 100
    CHECK(unpackSave(z.value(), 3011).isOk());
}
