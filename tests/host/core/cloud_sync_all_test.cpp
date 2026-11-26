#include "core/cloud_sync_all.hpp"

#include <doctest/doctest.h>

#include "core/cloud_state.hpp"
#include "support/fake_provider.hpp"
#include "support/mem_fs.hpp"

using namespace nl;
using nl::test::FakeProvider;
using nl::test::MemFileSystem;

namespace {

constexpr std::string_view kData = "sd:/newlauncher";
constexpr u64 kNow = 1764115200;
const std::string kAcct = "drive:4711";

std::vector<u8> bytes(std::string_view s) { return {s.begin(), s.end()}; }

std::string dirOf(std::string_view id4hex) { return "nand:/title/00010000/" + std::string(id4hex) + "/data"; }

SyncTarget target(std::string id, std::string_view hex, std::string title) {
    return SyncTarget{SaveUnit{std::move(id), SaveStore::RealNand, dirOf(hex), {}}, std::move(title)};
}

struct Rig {
    MemFileSystem fs;
    FakeProvider cloud;
    std::vector<std::string> logs;
    CloudCtx ctx{fs, std::string(kData), cloud, kAcct, "Wohnzimmer", kNow,
                 [this](std::string l) { logs.push_back(std::move(l)); }};
    volatile bool cancel = false;
    std::vector<std::pair<u32, u32>> progress;

    std::string text(const std::string& path) {
        const auto* f = fs.file(path);
        return f == nullptr ? std::string("<fehlt>") : std::string(f->data.begin(), f->data.end());
    }

    // Paket eines anderen Geräts.
    void remote(const std::string& unitId, std::string_view content) {
        const std::vector<SaveFile> files = {SaveFile{"a.bin", bytes(content)}};
        SaveMeta m;
        m.unit = unitId;
        m.title = "x";
        m.device = "Dolphin-PC";
        m.saved = kNow - 100;
        auto zip = packSave(m, files);
        REQUIRE(zip.isOk());
        cloud.put(unitId, zip.value(), contentHash(files), "Dolphin-PC", kNow - 100);
    }

    SyncAllResult run(std::span<const SyncTarget> t, ConflictPolicy p,
                      std::function<void(u32, u32)> hook = nullptr) {
        return syncAll(ctx, t, p, cancel, [&](u32 done, u32 total) {
            progress.emplace_back(done, total);
            if (hook) hook(done, total);
        });
    }
};

std::string localHash(std::string_view c) { return contentHash(std::vector<SaveFile>{{"a.bin", bytes(c)}}); }

}  // namespace

TEST_CASE("syncAll: gemischte Bibliothek zählt pulled, pushed, same; Fortschritt 1/3 2/3 3/3") {
    Rig r;
    const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy"),
                                       target("wii-SMNP", "534d4e50", "NSMB"),
                                       target("wii-RSBP", "52534250", "Brawl")};
    r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("nur-wii"));  // → hochladen
    r.remote("wii-SMNP", "nur-cloud");                             // → holen
    r.fs.addFile(dirOf("52534250") + "/a.bin", bytes("gleich"));   // → gleich
    r.remote("wii-RSBP", "gleich");
    const auto res = r.run(t, ConflictPolicy::Defer);
    CHECK(res.pushed == 1);
    CHECK(res.pulled == 1);
    CHECK(res.same == 1);
    CHECK(res.skipped == 0);
    CHECK(res.conflicts.empty());
    CHECK_FALSE(res.cancelled);
    CHECK_FALSE(res.stopped);
    CHECK(r.text(dirOf("534d4e50") + "/a.bin") == "nur-cloud");
    CHECK(r.progress == std::vector<std::pair<u32, u32>>{{1, 3}, {2, 3}, {3, 3}});
    CHECK(syncAllLine(res) == "NL_CLOUD_SYNCALL pulled=1 pushed=1 same=1 conflict=0 skip=0 cancel=0");
}

TEST_CASE("syncAll: Konflikt bei Defer gesammelt, nichts übertragen") {
    Rig r;
    const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy")};
    r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("wii"));
    r.remote("wii-RMGP", "pc");
    const auto res = r.run(t, ConflictPolicy::Defer);
    CHECK(res.conflicts == std::vector<std::string>{"wii-RMGP"});
    CHECK(r.cloud.uploads == 0);
    CHECK(r.text(dirOf("524d4750") + "/a.bin") == "wii");
    CHECK(syncAllLine(res) == "NL_CLOUD_SYNCALL pulled=0 pushed=0 same=0 conflict=1 skip=0 cancel=0");
}

TEST_CASE("syncAll: Konflikt bei KeepWii hochgeladen, bei TakeCloud geholt") {
    {
        Rig r;
        const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy")};
        r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("wii"));
        r.remote("wii-RMGP", "pc");
        const auto res = r.run(t, ConflictPolicy::KeepWii);
        CHECK(res.pushed == 1);
        CHECK(res.conflicts.empty());
        CHECK(r.cloud.files["wii-RMGP"].entry.hash == localHash("wii"));
    }
    {
        Rig r;
        const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy")};
        r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("wii"));
        r.remote("wii-RMGP", "pc");
        const auto res = r.run(t, ConflictPolicy::TakeCloud);
        CHECK(res.pulled == 1);
        CHECK(res.conflicts.empty());
        CHECK(r.text(dirOf("524d4750") + "/a.bin") == "pc");
    }
}

TEST_CASE("syncAll: offline beim zweiten Ziel → stopped, drittes nie angefragt") {
    Rig r;
    const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy"),
                                       target("wii-SMNP", "534d4e50", "NSMB"),
                                       target("wii-RSBP", "52534250", "Brawl")};
    r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("a"));
    r.fs.addFile(dirOf("534d4e50") + "/a.bin", bytes("b"));
    r.fs.addFile(dirOf("52534250") + "/a.bin", bytes("c"));
    const auto res = r.run(t, ConflictPolicy::Defer, [&](u32 done, u32) {
        if (done == 1) r.cloud.fail = CloudStatus::Offline;
    });
    CHECK(res.pushed == 1);
    CHECK(res.skipped == 1);
    CHECK(res.stopped);
    CHECK_FALSE(res.cancelled);
    CHECK(r.cloud.stats == 2);
    CHECK(r.cloud.files.count("wii-RSBP") == 0);
}

TEST_CASE("syncAll: abgemeldet → stopped") {
    Rig r;
    const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy"),
                                       target("wii-SMNP", "534d4e50", "NSMB")};
    r.cloud.fail = CloudStatus::Unauthorized;
    const auto res = r.run(t, ConflictPolicy::Defer);
    CHECK(res.stopped);
    CHECK(res.skipped == 1);
    CHECK(r.cloud.stats == 1);
}

TEST_CASE("syncAll: Abbruch nach dem ersten → cancelled, Rest nicht angefragt") {
    Rig r;
    const std::vector<SyncTarget> t = {target("wii-RMGP", "524d4750", "Galaxy"),
                                       target("wii-SMNP", "534d4e50", "NSMB"),
                                       target("wii-RSBP", "52534250", "Brawl")};
    r.fs.addFile(dirOf("524d4750") + "/a.bin", bytes("a"));
    const auto res = r.run(t, ConflictPolicy::Defer, [&](u32 done, u32) {
        if (done == 1) r.cancel = true;
    });
    CHECK(res.cancelled);
    CHECK_FALSE(res.stopped);
    CHECK(res.pushed == 1);
    CHECK(r.cloud.stats == 1);
    CHECK(syncAllLine(res) == "NL_CLOUD_SYNCALL pulled=0 pushed=1 same=0 conflict=0 skip=0 cancel=1");
}

TEST_CASE("syncAll: doppelte Einheit (eine Karte, zwei Spiele) nur einmal") {
    Rig r;
    const SaveUnit card{"gc-ninmem", SaveStore::Fat, "usb:/saves", {"ninmem.raw"}};
    const std::vector<SyncTarget> t = {SyncTarget{card, "Melee"}, SyncTarget{card, "Wind Waker"},
                                       target("wii-RMGP", "524d4750", "Galaxy")};
    r.fs.addFile("usb:/saves/ninmem.raw", bytes("karte"));
    const auto res = r.run(t, ConflictPolicy::Defer);
    CHECK(r.cloud.stats == 2);
    CHECK(r.progress.back() == std::pair<u32, u32>{2, 2});
}

TEST_CASE("Anbieterwechsel: Antwort → Regel, Debug-Text → Antwort, Konflikt-Ziele") {
    CHECK(switchPolicy(0) == ConflictPolicy::KeepWii);
    CHECK(switchPolicy(1) == ConflictPolicy::TakeCloud);
    CHECK(switchPolicy(2) == ConflictPolicy::Defer);
    CHECK(switchPolicy(-1) == ConflictPolicy::Defer);
    CHECK(parseSwitchAnswer("wii") == 0);
    CHECK(parseSwitchAnswer("cloud") == 1);
    CHECK(parseSwitchAnswer("ask") == 2);
    CHECK(parseSwitchAnswer("x") == -1);

    std::vector<SyncTarget> all(3);
    all[0].unit.id = "wii-RMCP";
    all[1].unit.id = "gc-GALE";
    all[2].unit.id = "ch-0001000148414145";
    const std::vector<std::string> ids = {"ch-0001000148414145", "wii-RMCP", "fehlt"};
    const auto sub = conflictTargets(all, ids);
    REQUIRE(sub.size() == 2);
    CHECK(sub[0].unit.id == "wii-RMCP");
    CHECK(sub[1].unit.id == "ch-0001000148414145");
}
