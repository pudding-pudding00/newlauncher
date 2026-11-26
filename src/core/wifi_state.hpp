#pragma once
// WLAN-Zustand und Wiederholungsregeln (M25, Spec 2026-11-26-m25-wlan-design.md). Ohne <ogc/...>: Der Netz-Thread
// (platform/net_service) füllt ein WifiSnapshot, die UI liest es; die Regeln hier sind auf dem Host testbar.

#include <array>
#include <cstddef>

#include "core/net_error.hpp"
#include "core/types.hpp"

namespace nl {

enum class WifiProbeKind : u8 { Initial, Job, Background, Reconnect };

// Versuche je Durchgang: Start und „Neu verbinden“ 4, vor einem Netzauftrag 3, Hintergrund-Check 2.
int wifiInitAttempts(WifiProbeKind kind);
// Pause nach dem n-ten Fehlschlag (ab 0): 500, 1000, 2000, dann 4000 ms.
u32 wifiRetryDelayMs(int failedAttempts);
// Abstand bis zum nächsten Hintergrund-Check: 5 s, ohne Verbindung je nach Fehlschlägen in Folge 5/5/10/20/30 s.
u32 wifiProbeIntervalMs(WifiStatus status, u32 failedProbes);

// Ergebnis eines (nicht blockierenden) net_connect zum Router; r ist eine negierte newlib-errno.
enum class GatewayReply : u8 { Connected, Pending, Refused, Failed };
GatewayReply classifyGatewayConnect(s32 r);
// Verbunden oder abgewiesen (RST): Der Router hat geantwortet.
inline bool gatewayReachable(GatewayReply r) { return r == GatewayReply::Connected || r == GatewayReply::Refused; }

inline constexpr u32 kGatewayFailsForLimited = 2;
WifiStatus wifiStatusFrom(bool ipUp, u32 gatewayFailsInRow);

struct WifiSnapshot {
    WifiStatus status = WifiStatus::Connecting;
    u32 ip = 0, netmask = 0, gateway = 0;  // Host-Byte-Reihenfolge, 0 = unbekannt
    std::array<u8, 6> mac{};
    bool macKnown = false;
    s32 lastInitCode = 0;    // letzter Rückgabewert von if_configex/net_init, < 0 = Fehler
    u32 failedProbes = 0;    // Durchgänge ohne Verbindung in Folge
    u32 gatewayFails = 0;    // Router-Checks ohne Antwort in Folge
    s32 gatewayMs = -1;      // letzte Router-Antwortzeit, -1 = keine
    u32 reconnects = 0;      // Neuaufbauten seit dem Start
    bool checked = false;    // mindestens ein Durchgang abgeschlossen
    u64 lastCheckMs = 0;     // Ende des letzten Durchgangs (Uhr des Aufrufers)
    u64 connectedSinceMs = 0;
    bool tlsBuild = false, tlsUp = false;
};

void formatIpv4(u32 ip, char* out, std::size_t n);  // "192.0.2.10", 0 → "-"
void formatMac(const std::array<u8, 6>& mac, char* out, std::size_t n);
void formatDuration(u64 ms, char* out, std::size_t n);  // "1:05", "1:02:03"

// Meldet Zustandswechsel für einen Hinweis; Connecting zählt nicht als Zustand.
enum class WifiEvent : u8 { None, Failed, Lost, Restored, Limited };
class WifiWatch {
public:
    WifiEvent update(WifiStatus s);

private:
    bool known_ = false;
    WifiStatus settled_ = WifiStatus::Connecting;
};

}  // namespace nl
