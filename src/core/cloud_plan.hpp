#pragma once
// Cloud-Abgleich einer Einheit (M27, Spec Ziel 5): vor dem Start (preLaunchSync), nach einer Konfliktwahl
// (resolveConflict) und nach dem Spielen (uploadIfChanged). Zustand in <data>/cloud/state.ini (atomar).
// Logzeilen nennen nur Einheit und Ergebnis, nie Token, Datei-ID oder Inhalt.

#include <functional>
#include <string>
#include <string_view>

#include "core/cloud_provider.hpp"
#include "core/fs.hpp"
#include "core/save_locator.hpp"

namespace nl {

struct CloudCtx {
    FileSystem& fs;
    std::string dataDir;
    CloudProvider& provider;
    std::string account;  // "<anbieter>:<konto-id>"
    std::string device;   // Gerätename für die Meta
    u64 now = 0;          // Unix-Zeit (nur Anzeige)
    std::function<void(std::string)> log;
};

enum class PreLaunch : u8 { Ready, Pulled, Pushed, Conflict, Skipped };

struct PreLaunchResult {
    PreLaunch kind = PreLaunch::Skipped;
    CloudEntry cloud;     // bei Conflict: Cloud-Seite (device, saved)
    u64 localPlayed = 0;  // bei Conflict: letzte Sitzung auf der Wii (0 = unbekannt)
    std::string reason;   // bei Skipped: offline|auth|nand|error
};

PreLaunchResult preLaunchSync(CloudCtx& ctx, const SaveUnit& unit, std::string_view title,
                              const volatile bool& cancel);

enum class ConflictChoice : u8 { KeepWii, TakeCloud };

// seenRev = Cloud-Revision, die der Dialog gezeigt hat. Ist die Cloud inzwischen eine andere, kommt wieder Conflict
// (mit der neuen Cloud-Seite), und nichts wird übertragen.
PreLaunchResult resolveConflict(CloudCtx& ctx, const SaveUnit& unit, std::string_view title, ConflictChoice choice,
                                std::string_view seenRev, const volatile bool& cancel);

// Ok auch, wenn nichts zu tun war. RevMismatch = Konflikt; der Eintrag bleibt in pending.ini, das Start-Tor fragt.
CloudStatus uploadIfChanged(CloudCtx& ctx, const SaveUnit& unit, std::string_view title, const volatile bool& cancel);

// Merkt die Zeit der letzten Sitzung (session.ini started) für den Konfliktdialog.
void notePlayed(CloudCtx& ctx, std::string_view unit, u64 unixTime);

}  // namespace nl
