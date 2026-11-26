#include "core/cloud_sync.hpp"

#include <doctest/doctest.h>

#include "core/cloud_outbox.hpp"
#include "core/cloud_state.hpp"

using namespace nl;

namespace {

CloudEntry cloud(std::string rev, std::string hash) {
    CloudEntry e;
    e.id = "Z1";
    e.rev = std::move(rev);
    e.hash = std::move(hash);
    return e;
}

SyncInput in(std::optional<std::string> local, std::optional<SyncBase> base, std::optional<CloudEntry> c) {
    return SyncInput{std::move(local), std::move(base), std::move(c)};
}

const SyncBase kBase{"H0", "5"};

}  // namespace

TEST_CASE("decideSync: Tabelle mit Basis") {
    CHECK(decideSync(in("H0", kBase, cloud("5", "H0"))) == SyncAction::Nothing);
    CHECK(decideSync(in("H0", kBase, cloud("6", "H1"))) == SyncAction::Download);
    CHECK(decideSync(in("H0", kBase, cloud("6", "H0"))) == SyncAction::SetBase);  // Cloud neu, gleicher Inhalt
    CHECK(decideSync(in("H1", kBase, cloud("5", "H0"))) == SyncAction::Upload);
    CHECK(decideSync(in("H1", kBase, cloud("6", "H1"))) == SyncAction::SetBase);
    CHECK(decideSync(in("H1", kBase, cloud("6", "H2"))) == SyncAction::Conflict);
}

TEST_CASE("decideSync: gleiche rev, aber anderer Cloud-Hash = Cloud neu") {
    // Absicherung, falls ein Anbieter gleiche Revisionen für verschiedene Stände liefert.
    CHECK(decideSync(in("H0", kBase, cloud("5", "H1"))) == SyncAction::Download);
    CHECK(decideSync(in("H2", kBase, cloud("5", "H1"))) == SyncAction::Conflict);
    CHECK(decideSync(in("H2", kBase, cloud("5", ""))) == SyncAction::Upload);  // ohne Hash zählt nur die rev
}

TEST_CASE("decideSync: ohne Basis") {
    CHECK(decideSync(in("H1", std::nullopt, std::nullopt)) == SyncAction::Upload);
    CHECK(decideSync(in(std::nullopt, std::nullopt, cloud("1", "H1"))) == SyncAction::Download);
    CHECK(decideSync(in("H1", std::nullopt, cloud("1", "H1"))) == SyncAction::SetBase);
    CHECK(decideSync(in("H1", std::nullopt, cloud("1", "H2"))) == SyncAction::Conflict);
    CHECK(decideSync(in(std::nullopt, std::nullopt, std::nullopt)) == SyncAction::Nothing);
}

TEST_CASE("decideSync: eine Seite fehlt trotz Basis") {
    // fehlt lokal, Cloud vorhanden → herunterladen (Löschungen werden nie übertragen)
    CHECK(decideSync(in(std::nullopt, kBase, cloud("5", "H0"))) == SyncAction::Download);
    CHECK(decideSync(in(std::nullopt, kBase, cloud("6", "H1"))) == SyncAction::Download);
    // in der Cloud gelöscht → lokal wieder hochladen
    CHECK(decideSync(in("H0", kBase, std::nullopt)) == SyncAction::Upload);
    CHECK(decideSync(in("H1", kBase, std::nullopt)) == SyncAction::Upload);
    CHECK(decideSync(in(std::nullopt, kBase, std::nullopt)) == SyncAction::Nothing);
}

TEST_CASE("syncActionName") {
    CHECK(std::string(syncActionName(SyncAction::Conflict)) == "conflict");
    CHECK(std::string(syncActionName(SyncAction::SetBase)) == "setbase");
}

TEST_CASE("CloudState: Rundlauf, Konten getrennt") {
    CloudState s;
    s.setBase("drive:111", "wii-RMCP", SyncBase{"H1", "7"});
    s.setPlayed("drive:111", "wii-RMCP", 1'700'000'000);
    s.setBase("drive:222", "wii-RMCP", SyncBase{"H2", "3"});
    const CloudState back = CloudState::parse(s.serialize());
    REQUIRE(back.base("drive:111", "wii-RMCP").has_value());
    CHECK(back.base("drive:111", "wii-RMCP")->hash == "H1");
    CHECK(back.base("drive:111", "wii-RMCP")->rev == "7");
    CHECK(back.played("drive:111", "wii-RMCP") == 1'700'000'000);
    CHECK(back.base("drive:222", "wii-RMCP")->hash == "H2");
    CHECK(back.played("drive:222", "wii-RMCP") == 0);
    CHECK_FALSE(back.base("drive:111", "wii-SMNP").has_value());
    CHECK_FALSE(back.base("onedrive:111", "wii-RMCP").has_value());
}

TEST_CASE("CloudState: unvollständige Basis zählt nicht") {
    const CloudState s = CloudState::parse("[drive:1/wii-RMCP]\nhash = H1\n");
    CHECK_FALSE(s.base("drive:1", "wii-RMCP").has_value());
}

TEST_CASE("CloudState: clearAccount entfernt nur dieses Konto") {
    CloudState s;
    s.setBase("drive:1", "a", SyncBase{"H", "1"});
    s.setBase("drive:1", "b", SyncBase{"H", "1"});
    s.setBase("drive:2", "a", SyncBase{"H", "1"});
    s.clearAccount("drive:1");
    CHECK_FALSE(s.base("drive:1", "a").has_value());
    CHECK_FALSE(s.base("drive:1", "b").has_value());
    CHECK(s.base("drive:2", "a").has_value());
}

TEST_CASE("Session: Rundlauf und kaputte Datei") {
    const SessionEntry e{"wii:RMCP01", "wii-RMCP", 1'700'000'123};
    const auto back = parseSession(serializeSession(e));
    REQUIRE(back.has_value());
    CHECK(back->gameKey == "wii:RMCP01");
    CHECK(back->unit == "wii-RMCP");
    CHECK(back->started == 1'700'000'123);
    CHECK_FALSE(parseSession("").has_value());
    CHECK_FALSE(parseSession("[session]\nunit = wii-RMCP\n").has_value());
    CHECK_FALSE(parseSession("[session]\ngame = wii:RMCP01\n").has_value());
}

TEST_CASE("Pending: eindeutig, Reihenfolge bleibt, Rundlauf") {
    std::vector<std::string> p;
    addPending(p, "b");
    addPending(p, "a");
    addPending(p, "b");
    REQUIRE(p.size() == 2);
    CHECK(p[0] == "b");
    CHECK(p[1] == "a");
    const auto back = parsePending(serializePending(p));
    CHECK(back == p);
    CHECK(parsePending("").empty());
    removePending(p, "b");
    CHECK(p == std::vector<std::string>{"a"});
}
