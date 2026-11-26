#include "core/wifi_state.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace nl;

TEST_CASE("wifiInitAttempts: Start großzügig, Hintergrund knapp") {
    CHECK(wifiInitAttempts(WifiProbeKind::Initial) == 4);
    CHECK(wifiInitAttempts(WifiProbeKind::Reconnect) == 4);
    CHECK(wifiInitAttempts(WifiProbeKind::Job) == 3);
    CHECK(wifiInitAttempts(WifiProbeKind::Background) == 2);
}

TEST_CASE("wifiRetryDelayMs: verdoppelt sich bis 4 s") {
    CHECK(wifiRetryDelayMs(0) == 500);
    CHECK(wifiRetryDelayMs(1) == 1000);
    CHECK(wifiRetryDelayMs(2) == 2000);
    CHECK(wifiRetryDelayMs(3) == 4000);
    CHECK(wifiRetryDelayMs(4) == 4000);
    CHECK(wifiRetryDelayMs(100) == 4000);
    CHECK(wifiRetryDelayMs(-1) == 500);
}

TEST_CASE("wifiProbeIntervalMs: verbunden 5 s, getrennt 5/5/10/20 s, dann 30 s") {
    CHECK(wifiProbeIntervalMs(WifiStatus::Connected, 0) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Connected, 7) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Limited, 0) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Connecting, 0) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 0) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 1) == 5000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 2) == 10000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 3) == 20000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 4) == 30000);
    CHECK(wifiProbeIntervalMs(WifiStatus::Disconnected, 1000) == 30000);
}

TEST_CASE("classifyGatewayConnect: abgewiesen heißt erreichbar") {
    CHECK(classifyGatewayConnect(0) == GatewayReply::Connected);
    CHECK(classifyGatewayConnect(-127) == GatewayReply::Connected);  // EISCONN
    CHECK(classifyGatewayConnect(-119) == GatewayReply::Pending);    // EINPROGRESS
    CHECK(classifyGatewayConnect(-120) == GatewayReply::Pending);    // EALREADY
    CHECK(classifyGatewayConnect(-11) == GatewayReply::Pending);     // EAGAIN
    CHECK(classifyGatewayConnect(-111) == GatewayReply::Refused);    // ECONNREFUSED
    CHECK(classifyGatewayConnect(-116) == GatewayReply::Failed);     // ETIMEDOUT
    CHECK(classifyGatewayConnect(-118) == GatewayReply::Failed);     // EHOSTUNREACH
    CHECK(classifyGatewayConnect(-1) == GatewayReply::Failed);
    CHECK(gatewayReachable(GatewayReply::Connected));
    CHECK(gatewayReachable(GatewayReply::Refused));
    CHECK_FALSE(gatewayReachable(GatewayReply::Failed));
    CHECK_FALSE(gatewayReachable(GatewayReply::Pending));
}

TEST_CASE("wifiStatusFrom: erst zwei Router-Fehlschläge in Folge ergeben Limited") {
    CHECK(wifiStatusFrom(false, 0) == WifiStatus::Disconnected);
    CHECK(wifiStatusFrom(false, 5) == WifiStatus::Disconnected);
    CHECK(wifiStatusFrom(true, 0) == WifiStatus::Connected);
    CHECK(wifiStatusFrom(true, 1) == WifiStatus::Connected);
    CHECK(wifiStatusFrom(true, 2) == WifiStatus::Limited);
    CHECK(wifiStatusFrom(true, 9) == WifiStatus::Limited);
}

TEST_CASE("formatIpv4 und formatMac") {
    char buf[32];
    formatIpv4(0xC000020Au, buf, sizeof buf);
    CHECK(std::string(buf) == "192.0.2.10");
    formatIpv4(0, buf, sizeof buf);
    CHECK(std::string(buf) == "-");
    const std::array<u8, 6> mac{0x00, 0x1f, 0x32, 0xab, 0xcd, 0xef};
    formatMac(mac, buf, sizeof buf);
    CHECK(std::string(buf) == "00:1F:32:AB:CD:EF");
}

TEST_CASE("formatDuration: mm:ss, ab einer Stunde h:mm:ss") {
    char buf[16];
    formatDuration(0, buf, sizeof buf);
    CHECK(std::string(buf) == "0:00");
    formatDuration(65 * 1000, buf, sizeof buf);
    CHECK(std::string(buf) == "1:05");
    formatDuration((3600 + 2 * 60 + 3) * 1000ull, buf, sizeof buf);
    CHECK(std::string(buf) == "1:02:03");
}

TEST_CASE("WifiWatch: meldet nur Zustandswechsel, Connecting zählt nicht") {
    WifiWatch w;
    CHECK(w.update(WifiStatus::Connecting) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::Failed);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Connecting) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::None);  // weitere Fehlschläge still
    CHECK(w.update(WifiStatus::Connecting) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Connected) == WifiEvent::Restored);
    CHECK(w.update(WifiStatus::Connected) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::Lost);
    CHECK(w.update(WifiStatus::Connecting) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Connected) == WifiEvent::Restored);
    CHECK(w.update(WifiStatus::Limited) == WifiEvent::Limited);
    CHECK(w.update(WifiStatus::Limited) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Connected) == WifiEvent::Restored);
    CHECK(w.update(WifiStatus::Limited) == WifiEvent::Limited);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::Lost);
}

TEST_CASE("WifiWatch: erster Erfolg ist kein Restored") {
    WifiWatch w;
    CHECK(w.update(WifiStatus::Connecting) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Connected) == WifiEvent::None);
    CHECK(w.update(WifiStatus::Disconnected) == WifiEvent::Lost);
}

TEST_CASE("WifiSnapshot: Startwert ist Connecting") {
    const WifiSnapshot s;
    CHECK(s.status == WifiStatus::Connecting);
    CHECK(s.gatewayMs < 0);
    CHECK_FALSE(s.checked);
}
