#include "core/cloud_gate.hpp"

#include <doctest/doctest.h>

using nl::cloudGateExpired;
using nl::CloudGateClock;

TEST_CASE("cloudGateExpired: 20 s für die Abfrage") {
    CHECK_FALSE(cloudGateExpired(CloudGateClock{.elapsedMs = 20000}));
    CHECK(cloudGateExpired(CloudGateClock{.elapsedMs = 20001}));
}

TEST_CASE("cloudGateExpired: Übertragung läuft → kein Zeitlimit (B bricht weiter ab)") {
    CHECK_FALSE(cloudGateExpired(CloudGateClock{.elapsedMs = 600000, .transferring = true}));
}

TEST_CASE("cloudGateExpired: Konfliktdialog offen → kein Zeitlimit") {
    CHECK_FALSE(cloudGateExpired(CloudGateClock{.elapsedMs = 600000, .asking = true}));
}

TEST_CASE("cloudGateExpired: Warten auf den abgebrochenen Upload zählt mit, auch wenn er überträgt") {
    // Der alte Auftrag ist nicht der des Tors; seine Übertragung darf das Tor nicht offen halten.
    CHECK(cloudGateExpired(CloudGateClock{.elapsedMs = 20001, .transferring = true, .waitingForOld = true}));
}
