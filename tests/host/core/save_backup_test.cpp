#include "core/save_backup.hpp"
#include <doctest/doctest.h>
#include "support/failing_fs.hpp"
using namespace nl;
using nl::test::FailingFs;
using nl::test::MemFileSystem;

namespace {
constexpr std::string_view kData = "sd:/newlauncher";
constexpr u64 kNow = 1764115200;  // 2025-11-26 00:00:00 UTC
const std::string kDir = "nand:/title/00010000/524d4750/data";

std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

SaveUnit unit() { return SaveUnit{"wii-RMGP", SaveStore::RealNand, kDir, {}}; }

void seed(MemFileSystem& fs) {
    fs.addFile(kDir + "/a.bin", bytes("A-alt"));
    fs.addFile(kDir + "/b.bin", bytes("B-alt"));
    fs.addFile(kDir + "/lokal.bin", bytes("nur lokal"));
}

SavePackage package(std::string unitId = "wii-RMGP") {
    SavePackage p;
    p.files = {SaveFile{"a.bin", bytes("A-neu")}, SaveFile{"b.bin", bytes("B-neu")}, SaveFile{"sub/c.bin", bytes("C-neu")}};
    p.meta.unit = std::move(unitId);
    p.meta.hash = contentHash(p.files);
    p.meta.files = 3;
    return p;
}

std::string text(MemFileSystem& fs, const std::string& path) {
    const auto* f = fs.file(path);
    return f == nullptr ? std::string("<fehlt>") : std::string(f->data.begin(), f->data.end());
}

void checkOriginal(MemFileSystem& fs) {
    CHECK(text(fs, kDir + "/a.bin") == "A-alt");
    CHECK(text(fs, kDir + "/b.bin") == "B-alt");
    CHECK(text(fs, kDir + "/lokal.bin") == "nur lokal");
    CHECK(fs.file(kDir + "/sub/c.bin") == nullptr);
    CHECK(fs.file(std::string(kData) + "/saves/restore.ini") == nullptr);
}
}  // namespace

TEST_CASE("backupFileName und backupDir") {
    CHECK(backupFileName(kNow) == "20251126-000000.zip");
    CHECK(backupFileName(kNow + 3661) == "20251126-010101.zip");
    CHECK(backupDir(kData, "wii-RMGP") == "sd:/newlauncher/saves/wii-RMGP");
}

TEST_CASE("backupUnit: sechste Sicherung löscht die älteste, Liste neueste zuerst") {
    MemFileSystem fs;
    seed(fs);
    for (u64 i = 0; i < 6; ++i) {
        auto b = backupUnit(fs, kData, unit(), "Galaxy", "Wii", kNow + i * 60);
        CHECK(b.isOk());
        if (b.isOk()) CHECK(!b.value().empty);
    }
    const auto list = listBackups(fs, kData, "wii-RMGP");
    CHECK(list.size() == 5);
    if (list.size() == 5) {
        CHECK(list.front() == "20251126-000500.zip");
        CHECK(list.back() == "20251126-000100.zip");
    }
}

TEST_CASE("backupUnit: gleiche Sekunde überschreibt keine Sicherung") {
    MemFileSystem fs;
    seed(fs);
    CHECK(backupUnit(fs, kData, unit(), "", "", kNow).isOk());
    CHECK(backupUnit(fs, kData, unit(), "", "", kNow).isOk());
    CHECK(listBackups(fs, kData, "wii-RMGP").size() == 2);
}

TEST_CASE("backupUnit: leere Einheit legt kein ZIP an") {
    MemFileSystem fs;
    auto b = backupUnit(fs, kData, unit(), "", "", kNow);
    CHECK(b.isOk());
    if (b.isOk()) {
        CHECK(b.value().empty);
        CHECK(b.value().zipPath.empty());
    }
    CHECK(listBackups(fs, kData, "wii-RMGP").empty());
}

TEST_CASE("Journal: hin und zurück") {
    RestoreJournal j{"wii-RMGP", kDir, "sd:/newlauncher/saves/wii-RMGP/x.zip", {"sub/c.bin", "d.bin"}, RestorePhase::Verifying};
    auto p = parseJournal(serializeJournal(j));
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->unit == j.unit);
    CHECK(p->dir == j.dir);
    CHECK(p->backup == j.backup);
    CHECK(p->added == j.added);
    CHECK(p->phase == RestorePhase::Verifying);
    CHECK(!parseJournal("[restore]\nunit = x\n").has_value());
}

TEST_CASE("restoreUnit: Rundlauf, lokale Datei bleibt, Sicherung vorher") {
    MemFileSystem fs;
    seed(fs);
    const SavePackage pkg = package();
    CHECK(restoreUnit(fs, kData, unit(), pkg, "Galaxy", "Wii", kNow).isOk());
    CHECK(text(fs, kDir + "/a.bin") == "A-neu");
    CHECK(text(fs, kDir + "/sub/c.bin") == "C-neu");
    CHECK(text(fs, kDir + "/lokal.bin") == "nur lokal");
    CHECK(fs.file(std::string(kData) + "/saves/restore.ini") == nullptr);
    auto files = readUnit(fs, unit());
    CHECK(files.isOk());
    const auto list = listBackups(fs, kData, "wii-RMGP");
    CHECK(list.size() == 1);
    if (list.empty()) return;
    // Die Sicherung enthält den alten Stand.
    const auto* zip = fs.file(backupDir(kData, "wii-RMGP") + "/" + list[0]);
    CHECK(zip != nullptr);
    if (zip == nullptr) return;
    auto old = unpackSave(zip->data);
    CHECK(old.isOk());
    if (old.isOk()) CHECK(old.value().files.size() == 3);
}

TEST_CASE("restoreUnit: Fehler nach Datei 2 von 3 rollt zurück") {
    MemFileSystem mem;
    seed(mem);
    FailingFs fs(mem);
    fs.failWriteAfter = 4;  // Sicherung, Journal, a.bin, b.bin – sub/c.bin scheitert
    auto r = restoreUnit(fs, kData, unit(), package(), "", "", kNow);
    CHECK(!r.isOk());
    CHECK(fs.writeCalls == 8);  // 5 bis zum Fehler beim dritten Spielstand, dann 3 Dateien aus der Sicherung zurück
    CHECK(listBackups(mem, kData, "wii-RMGP").size() == 1);
    checkOriginal(mem);
}

TEST_CASE("restoreUnit: falsche Einheit oder unsicherer Pfad schreibt nichts") {
    MemFileSystem fs;
    seed(fs);
    const int before = fs.writes;
    CHECK(!restoreUnit(fs, kData, unit(), package("wii-XXXX"), "", "", kNow).isOk());
    SavePackage evil = package();
    evil.files[2].path = "../../shared2/x";
    evil.meta.hash = contentHash(evil.files);
    CHECK(!restoreUnit(fs, kData, unit(), evil, "", "", kNow).isOk());
    SaveUnit gb{"gb-x-00000000", SaveStore::Fat, "sd:/vbagx/saves", {"Tetris.sav"}};
    SavePackage dol;
    dol.files = {SaveFile{"boot.dol", bytes("x")}};
    dol.meta.unit = gb.id;
    dol.meta.hash = contentHash(dol.files);
    dol.meta.files = 1;
    CHECK(!restoreUnit(fs, kData, gb, dol, "", "", kNow).isOk());
    CHECK(fs.writes == before);
}

TEST_CASE("recoverRestore: liegengebliebenes Journal rollt zurück") {
    MemFileSystem fs;
    seed(fs);
    CHECK(recoverRestore(fs, kData) == RecoverResult::None);
    auto b = backupUnit(fs, kData, unit(), "", "", kNow);
    CHECK(b.isOk());
    if (!b.isOk()) return;
    const RestoreJournal j{"wii-RMGP", kDir, b.value().zipPath, {"sub/c.bin"}, RestorePhase::Writing};
    const std::string jt = serializeJournal(j);
    fs.addFile(std::string(kData) + "/saves/restore.ini", std::vector<u8>(jt.begin(), jt.end()));
    // Absturz mitten im Schreiben: a.bin schon neu, c.bin neu angelegt.
    fs.addFile(kDir + "/a.bin", bytes("A-neu"));
    fs.addFile(kDir + "/sub/c.bin", bytes("C-neu"));
    CHECK(recoverRestore(fs, kData) == RecoverResult::RolledBack);
    checkOriginal(fs);
}

TEST_CASE("recoverRestore: leere Sicherung löscht nur die neuen Dateien") {
    MemFileSystem fs;
    const RestoreJournal j{"wii-RMGP", kDir, "", {"a.bin"}, RestorePhase::Verifying};
    const std::string jt = serializeJournal(j);
    fs.addFile(std::string(kData) + "/saves/restore.ini", std::vector<u8>(jt.begin(), jt.end()));
    fs.addFile(kDir + "/a.bin", bytes("A-neu"));
    CHECK(recoverRestore(fs, kData) == RecoverResult::RolledBack);
    CHECK(fs.file(kDir + "/a.bin") == nullptr);
    CHECK(fs.file(std::string(kData) + "/saves/restore.ini") == nullptr);
}

TEST_CASE("restoreFromZip: Sicherung zurückspielen, kaputtes ZIP schreibt nichts") {
    MemFileSystem fs;
    seed(fs);
    auto b = backupUnit(fs, kData, unit(), "Galaxy", "Wii", kNow);
    CHECK(b.isOk());
    if (!b.isOk()) return;
    fs.addFile(kDir + "/a.bin", bytes("A-später"));
    CHECK(restoreFromZip(fs, kData, unit(), b.value().zipPath, "Galaxy", "Wii", kNow + 60).isOk());
    CHECK(text(fs, kDir + "/a.bin") == "A-alt");
    CHECK(listBackups(fs, kData, "wii-RMGP").size() == 2);  // der Stand davor ist auch gesichert

    fs.addFile(std::string(kData) + "/kaputt.zip", bytes("kein zip"));
    const int before = fs.writes;
    CHECK(!restoreFromZip(fs, kData, unit(), std::string(kData) + "/kaputt.zip", "", "", kNow).isOk());
    CHECK(!restoreFromZip(fs, kData, unit(), std::string(kData) + "/fehlt.zip", "", "", kNow).isOk());
    CHECK(fs.writes == before);
}
