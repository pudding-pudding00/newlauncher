#pragma once

#include "core/types.hpp"

namespace nl {

// Zeitlimit des Start-Tors: begrenzt nur die Abfrage (Ordner, Stat). Läuft die Übertragung, wartet das Tor, bis sie
// fertig ist; B bricht weiter jederzeit ab. Während der Konfliktdialog offen ist, läuft keine Uhr.
struct CloudGateClock {
    u32 elapsedMs = 0;
    bool transferring = false;   // Auftrag hat mit Download/Upload begonnen
    bool asking = false;         // Konfliktdialog offen
    bool waitingForOld = false;  // Tor wartet noch auf einen abgebrochenen fremden Auftrag
};

inline constexpr u32 kCloudGateLimitMs = 20000;

constexpr bool cloudGateExpired(const CloudGateClock& c) {
    if (c.asking) return false;
    // Die Übertragung eines fremden Auftrags hält das Tor nicht offen.
    if (c.transferring && !c.waitingForOld) return false;
    return c.elapsedMs > kCloudGateLimitMs;
}

}  // namespace nl
