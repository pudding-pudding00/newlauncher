#include "core/cloud_plan.hpp"

#include <doctest/doctest.h>

#include "core/cloud_state.hpp"
#include "core/save_backup.hpp"
#include "support/fake_provider.hpp"
#include "support/mem_fs.hpp"

using namespace nl;
using nl::test::FakeProvider;
using nl::test::MemFileSystem;

namespace {

constexpr std::string_view kData = "sd:/newlauncher";
constexpr u64 kNow = 1764115200;
const std::string kDir = "nand:/title/00010000/524d4750/data";
const std::string kAcct = "drive:4711";

std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

SaveUnit unit() { return SaveUnit{"wii-RMGP", SaveStore::RealNand, kDir, {}}; }

struct Rig {
    MemFileSystem fs;
    FakeProvider cloud;
    std::vector<std::string> logs;
    CloudCtx ctx{fs, std::string(kData), cloud, kAcct, "Wohnzimmer", kNow,
                 [this](std::string l) { logs.push_back(std::move(l)); }};
    volatile bool cancel = false;

    std::string text(const std::string& path) {
        const auto* f = fs.file(path);
        return f == nullptr ? std::string("<fehlt>") : std::string(f->data.begin(), f->data.end());
    }
    CloudState state() {
        const auto* f = fs.file(std::string(kData) + "/cloud/state.ini");
        return CloudState::parse(f == nullptr ? std::string() : std::string(f->data.begin(), f->data.end()));
    }
};

// Paket, wie es ein anderes Gerät hochgeladen hätte.
void remote(FakeProvider& cloud, std::string_view content, std::string unitId = "wii-RMGP",
            std::string device = "Dolphin-PC", u64 saved = kNow - 100) {
    const std::vector<SaveFile> files = {SaveFile{"a.bin", bytes(content)}};
    SaveMeta m;
    m.unit = std::move(unitId);
    m.title = "Galaxy";
    m.device = device;
    m.saved = saved;
    auto zip = packSave(m, files);
    REQUIRE(zip.isOk());
    cloud.put("wii-RMGP", zip.value(), contentHash(files), device, saved);
}

std::string localHash(std::string_view content) { return contentHash(std::vector<SaveFile>{{"a.bin", bytes(content)}}); }

}  // namespace

TEST_CASE("preLaunchSync: nur lokal → Upload, Basis gesetzt; zweiter Lauf → Ready ohne Upload") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
    const auto first = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(first.kind == PreLaunch::Pushed);
    CHECK(r.cloud.uploads == 1);
    REQUIRE(r.cloud.files.count("wii-RMGP") == 1);
    CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("lokal"));
    CHECK(r.cloud.files["wii-RMGP"].entry.device == "Wohnzimmer");
    const auto base = r.state().base(kAcct, "wii-RMGP");
    REQUIRE(base.has_value());
    CHECK(base->hash == localHash("lokal"));
    CHECK(base->rev == r.cloud.files["wii-RMGP"].entry.rev);

    const auto second = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(second.kind == PreLaunch::Ready);
    CHECK(r.cloud.uploads == 1);
}

TEST_CASE("preLaunchSync: Cloud neuer → Pulled, Datei hat Cloud-Bytes, Sicherung existiert") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("alt"));
    remote(r.cloud, "alt");
    CloudState s;
    s.setBase(kAcct, "wii-RMGP", SyncBase{localHash("alt"), r.cloud.files["wii-RMGP"].entry.rev});
    const std::string st = s.serialize();
    r.fs.addFile(std::string(kData) + "/cloud/state.ini", bytes(st));
    remote(r.cloud, "neu aus der Cloud");

    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Pulled);
    CHECK(r.text(kDir + "/a.bin") == "neu aus der Cloud");
    CHECK_FALSE(listBackups(r.fs, kData, "wii-RMGP").empty());
    const auto base = r.state().base(kAcct, "wii-RMGP");
    REQUIRE(base.has_value());
    CHECK(base->hash == localHash("neu aus der Cloud"));
    CHECK(base->rev == r.cloud.files["wii-RMGP"].entry.rev);
}

TEST_CASE("preLaunchSync: fehlt lokal → Pulled") {
    Rig r;
    remote(r.cloud, "aus der Cloud");
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Pulled);
    CHECK(r.text(kDir + "/a.bin") == "aus der Cloud");
}

TEST_CASE("preLaunchSync: beide neu → Conflict mit Gerät, Zeit und letzter Sitzung") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("wii-stand"));
    remote(r.cloud, "pc-stand", "wii-RMGP", "Dolphin-PC", kNow - 500);
    notePlayed(r.ctx, "wii-RMGP", kNow - 60);
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    REQUIRE(res.kind == PreLaunch::Conflict);
    CHECK(res.cloud.device == "Dolphin-PC");
    CHECK(res.cloud.saved == kNow - 500);
    CHECK(res.localPlayed == kNow - 60);
    CHECK(r.text(kDir + "/a.bin") == "wii-stand");
    CHECK(r.cloud.uploads == 0);
}

TEST_CASE("resolveConflict: KeepWii lädt mit erwarteter Revision hoch, TakeCloud spielt zurück") {
    {
        Rig r;
        r.fs.addFile(kDir + "/a.bin", bytes("wii-stand"));
        remote(r.cloud, "pc-stand");
        const std::string seen = r.cloud.files["wii-RMGP"].entry.rev;
        const auto res = resolveConflict(r.ctx, unit(), "Galaxy", ConflictChoice::KeepWii, seen, r.cancel);
        CHECK(res.kind == PreLaunch::Pushed);
        CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("wii-stand"));
        CHECK(r.state().base(kAcct, "wii-RMGP")->hash == localHash("wii-stand"));
    }
    {
        Rig r;
        r.fs.addFile(kDir + "/a.bin", bytes("wii-stand"));
        remote(r.cloud, "pc-stand");
        const std::string seen = r.cloud.files["wii-RMGP"].entry.rev;
        const auto res = resolveConflict(r.ctx, unit(), "Galaxy", ConflictChoice::TakeCloud, seen, r.cancel);
        CHECK(res.kind == PreLaunch::Pulled);
        CHECK(r.text(kDir + "/a.bin") == "pc-stand");
        CHECK(r.state().base(kAcct, "wii-RMGP")->hash == localHash("pc-stand"));
    }
}

TEST_CASE("resolveConflict: Cloud hat sich seit dem Dialog geändert → wieder Conflict, nichts übertragen") {
    for (const ConflictChoice choice : {ConflictChoice::KeepWii, ConflictChoice::TakeCloud}) {
        Rig r;
        r.fs.addFile(kDir + "/a.bin", bytes("wii-stand"));
        remote(r.cloud, "pc-stand");
        const std::string seen = r.cloud.files["wii-RMGP"].entry.rev;  // das zeigte der Dialog
        remote(r.cloud, "zweite-wii", "wii-RMGP", "Wii Kinderzimmer");  // kam hoch, während der Dialog offen war
        REQUIRE(r.cloud.files["wii-RMGP"].entry.rev != seen);
        const auto res = resolveConflict(r.ctx, unit(), "Galaxy", choice, seen, r.cancel);
        CHECK(res.kind == PreLaunch::Conflict);
        CHECK(res.cloud.device == "Wii Kinderzimmer");
        CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("zweite-wii"));
        CHECK(r.text(kDir + "/a.bin") == "wii-stand");
    }
}

TEST_CASE("preLaunchSync: Paket einer fremden Einheit → Skipped, nichts geschrieben") {
    Rig r;
    remote(r.cloud, "fremd", "wii-SMNP");
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Skipped);
    CHECK(res.reason == "error");
    CHECK(r.fs.file(kDir + "/a.bin") == nullptr);
    CHECK_FALSE(r.state().base(kAcct, "wii-RMGP").has_value());
}

TEST_CASE("preLaunchSync: Inhalt passt nicht zu appProperties.hash → Skipped") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
    remote(r.cloud, "cloud");
    r.cloud.files["wii-RMGP"].entry.hash = localHash("etwas anderes");
    CloudState s;
    s.setBase(kAcct, "wii-RMGP", SyncBase{localHash("lokal"), "0"});
    r.fs.addFile(std::string(kData) + "/cloud/state.ini", bytes(s.serialize()));
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Skipped);
    CHECK(r.text(kDir + "/a.bin") == "lokal");
}

TEST_CASE("preLaunchSync: kaputtes Paket → Skipped, nichts geschrieben") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
    r.cloud.put("wii-RMGP", bytes("kein zip"), localHash("x"), "PC", kNow);
    CloudState s;
    s.setBase(kAcct, "wii-RMGP", SyncBase{localHash("lokal"), "0"});
    r.fs.addFile(std::string(kData) + "/cloud/state.ini", bytes(s.serialize()));
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Skipped);
    CHECK(r.text(kDir + "/a.bin") == "lokal");
}

TEST_CASE("preLaunchSync: RevMismatch beim Upload und Cloud danach neu → Conflict") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("wii-stand"));
    r.cloud.beforeUpload = [](FakeProvider& c) {
        const std::vector<SaveFile> files = {SaveFile{"a.bin", bytes("anderes Gerät")}};
        SaveMeta m;
        m.unit = "wii-RMGP";
        auto zip = packSave(m, files);
        c.put("wii-RMGP", zip.value(), contentHash(files), "Handy", kNow);
    };
    const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Conflict);
    CHECK(res.cloud.device == "Handy");
    CHECK(r.text(kDir + "/a.bin") == "wii-stand");
}

TEST_CASE("preLaunchSync: offline und abgemeldet → Skipped mit Grund") {
    {
        Rig r;
        r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
        r.cloud.fail = CloudStatus::Offline;
        const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
        CHECK(res.kind == PreLaunch::Skipped);
        CHECK(res.reason == "offline");
    }
    {
        Rig r;
        r.cloud.fail = CloudStatus::Unauthorized;
        const auto res = preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
        CHECK(res.kind == PreLaunch::Skipped);
        CHECK(res.reason == "auth");
    }
}

TEST_CASE("preLaunchSync: NAND nicht lesbar → Skipped nand") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
    SaveUnit u = unit();
    u.names = {"a.bin"};
    r.fs.failReadsUnder = "nand:";
    const auto res = preLaunchSync(r.ctx, u, "Galaxy", r.cancel);
    CHECK(res.kind == PreLaunch::Skipped);
    CHECK(res.reason == "nand");
    CHECK(r.cloud.stats == 0);
}

TEST_CASE("uploadIfChanged: unverändert → Ok ohne Netz; geändert → Upload; Konflikt → RevMismatch") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("v1"));
    REQUIRE(preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel).kind == PreLaunch::Pushed);
    const int statsBefore = r.cloud.stats;
    CHECK(uploadIfChanged(r.ctx, unit(), "Galaxy", r.cancel) == CloudStatus::Ok);
    CHECK(r.cloud.stats == statsBefore);

    r.fs.addFile(kDir + "/a.bin", bytes("v2"));
    CHECK(uploadIfChanged(r.ctx, unit(), "Galaxy", r.cancel) == CloudStatus::Ok);
    CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("v2"));
    CHECK(r.state().base(kAcct, "wii-RMGP")->hash == localHash("v2"));

    r.fs.addFile(kDir + "/a.bin", bytes("v3"));
    remote(r.cloud, "pc");
    CHECK(uploadIfChanged(r.ctx, unit(), "Galaxy", r.cancel) == CloudStatus::RevMismatch);
    CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("pc"));

    r.cloud.fail = CloudStatus::Offline;
    CHECK(uploadIfChanged(r.ctx, unit(), "Galaxy", r.cancel) == CloudStatus::Offline);
}

TEST_CASE("cloud_plan: kein Token im Log") {
    Rig r;
    r.fs.addFile(kDir + "/a.bin", bytes("lokal"));
    preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    remote(r.cloud, "pc");
    preLaunchSync(r.ctx, unit(), "Galaxy", r.cancel);
    REQUIRE_FALSE(r.logs.empty());
    for (const std::string& l : r.logs) {
        CHECK(l.find(FakeProvider::kToken) == std::string::npos);
        CHECK(l.find("id-wii") == std::string::npos);  // keine Datei-IDs
        CHECK(l.find("wii-RMGP") != std::string::npos);
    }
}
