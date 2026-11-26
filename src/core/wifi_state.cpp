#include "core/wifi_state.hpp"

#include <cstdio>

namespace nl {

int wifiInitAttempts(WifiProbeKind kind) {
    switch (kind) {
        case WifiProbeKind::Initial:
        case WifiProbeKind::Reconnect: return 4;
        case WifiProbeKind::Job: return 3;
        case WifiProbeKind::Background: return 2;
    }
    return 2;
}

u32 wifiRetryDelayMs(int failedAttempts) {
    if (failedAttempts <= 0) return 500;
    if (failedAttempts >= 3) return 4000;
    return 500u << failedAttempts;
}

u32 wifiProbeIntervalMs(WifiStatus status, u32 failedProbes) {
    if (status != WifiStatus::Disconnected || failedProbes <= 1) return 5000;
    if (failedProbes == 2) return 10000;
    if (failedProbes == 3) return 20000;
    return 30000;
}

GatewayReply classifyGatewayConnect(s32 r) {
    switch (r) {
        case 0:
        case -127: return GatewayReply::Connected;  // EISCONN
        case -119:                                  // EINPROGRESS
        case -120:                                  // EALREADY
        case -11: return GatewayReply::Pending;     // EAGAIN
        case -111: return GatewayReply::Refused;    // ECONNREFUSED
        default: return GatewayReply::Failed;
    }
}

WifiStatus wifiStatusFrom(bool ipUp, u32 gatewayFailsInRow) {
    if (!ipUp) return WifiStatus::Disconnected;
    return gatewayFailsInRow >= kGatewayFailsForLimited ? WifiStatus::Limited : WifiStatus::Connected;
}

void formatIpv4(u32 ip, char* out, std::size_t n) {
    if (ip == 0) {
        std::snprintf(out, n, "-");
        return;
    }
    std::snprintf(out, n, "%u.%u.%u.%u", (ip >> 24) & 0xffu, (ip >> 16) & 0xffu, (ip >> 8) & 0xffu, ip & 0xffu);
}

void formatMac(const std::array<u8, 6>& m, char* out, std::size_t n) {
    std::snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

void formatDuration(u64 ms, char* out, std::size_t n) {
    const u64 s = ms / 1000;
    const unsigned h = static_cast<unsigned>(s / 3600), m = static_cast<unsigned>((s / 60) % 60),
                   sec = static_cast<unsigned>(s % 60);
    if (h > 0) std::snprintf(out, n, "%u:%02u:%02u", h, m, sec);
    else std::snprintf(out, n, "%u:%02u", m, sec);
}

WifiEvent WifiWatch::update(WifiStatus s) {
    if (s == WifiStatus::Connecting) return WifiEvent::None;
    if (known_ && s == settled_) return WifiEvent::None;
    const bool first = !known_;
    known_ = true;
    settled_ = s;
    switch (s) {
        case WifiStatus::Disconnected: return first ? WifiEvent::Failed : WifiEvent::Lost;
        case WifiStatus::Limited: return WifiEvent::Limited;
        case WifiStatus::Connected:
            return first ? WifiEvent::None : WifiEvent::Restored;
        case WifiStatus::Connecting: break;
    }
    return WifiEvent::None;
}

}  // namespace nl
