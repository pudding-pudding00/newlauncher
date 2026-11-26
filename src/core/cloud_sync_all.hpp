#pragma once
// "Alle abgleichen" (M28): jede Spielstandseinheit der Bibliothek einmal durch preLaunchSync. Konflikte werden je
// nach Regel gesammelt (einmal fragen) oder gleich gelöst. Offline oder abgemeldet beendet den Lauf, Abbruch greift
// zwischen zwei Einheiten.

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "core/cloud_plan.hpp"

namespace nl {

struct SyncTarget {
    SaveUnit unit;
    std::string title;
};

enum class ConflictPolicy : u8 { Defer, KeepWii, TakeCloud };

struct SyncAllResult {
    u32 pulled = 0, pushed = 0, same = 0, skipped = 0;
    std::vector<std::string> conflicts;  // Einheiten-IDs (nur bei Defer)
    bool cancelled = false, stopped = false;
};

SyncAllResult syncAll(CloudCtx& ctx, std::span<const SyncTarget> targets, ConflictPolicy policy,
                      const volatile bool& cancel, const std::function<void(u32 done, u32 total)>& progress);

// NL_CLOUD_SYNCALL pulled=N pushed=N same=N conflict=N skip=N cancel=0|1
std::string syncAllLine(const SyncAllResult& r);

}  // namespace nl
