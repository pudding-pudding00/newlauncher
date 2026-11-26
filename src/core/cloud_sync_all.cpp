#include "core/cloud_sync_all.hpp"

#include <set>

namespace nl {

SyncAllResult syncAll(CloudCtx& ctx, std::span<const SyncTarget> targets, ConflictPolicy policy,
                      const volatile bool& cancel, const std::function<void(u32 done, u32 total)>& progress) {
    // Eine Speicherkarte für mehrere Spiele (gc-ninmem) ist eine Einheit: nur einmal abgleichen.
    std::vector<const SyncTarget*> todo;
    std::set<std::string> seen;
    for (const SyncTarget& t : targets) {
        if (seen.insert(t.unit.id).second) todo.push_back(&t);
    }

    SyncAllResult res;
    const u32 total = static_cast<u32>(todo.size());
    u32 done = 0;
    for (const SyncTarget* t : todo) {
        if (cancel) {
            res.cancelled = true;
            break;
        }
        PreLaunchResult r = preLaunchSync(ctx, t->unit, t->title, cancel);
        if (r.kind == PreLaunch::Conflict && policy != ConflictPolicy::Defer) {
            const ConflictChoice c =
                policy == ConflictPolicy::KeepWii ? ConflictChoice::KeepWii : ConflictChoice::TakeCloud;
            r = resolveConflict(ctx, t->unit, t->title, c, r.cloud.rev, cancel);
        }
        switch (r.kind) {
            case PreLaunch::Ready: ++res.same; break;
            case PreLaunch::Pulled: ++res.pulled; break;
            case PreLaunch::Pushed: ++res.pushed; break;
            case PreLaunch::Conflict: res.conflicts.push_back(t->unit.id); break;
            case PreLaunch::Skipped: ++res.skipped; break;
        }
        ++done;
        if (progress) progress(done, total);
        if (r.kind == PreLaunch::Skipped && (r.reason == "offline" || r.reason == "auth")) {
            res.stopped = true;  // jede weitere Einheit scheiterte genauso
            res.stoppedAuth = r.reason == "auth";
            break;
        }
    }
    // Abbruch während der letzten Einheit: der Transfer endete als Fehler, die Schleife sah cancel nicht mehr.
    if (cancel && !res.stopped) res.cancelled = true;
    return res;
}

SyncAllEnd syncAllEnd(const SyncAllResult& r) {
    if (r.cancelled) return SyncAllEnd::Cancelled;
    if (r.stopped) return r.stoppedAuth ? SyncAllEnd::Auth : SyncAllEnd::Offline;
    return SyncAllEnd::Done;
}

bool askSwitch(const SyncAllResult& r) { return !r.conflicts.empty() && syncAllEnd(r) == SyncAllEnd::Done; }

std::string syncAllLine(const SyncAllResult& r) {
    return "NL_CLOUD_SYNCALL pulled=" + std::to_string(r.pulled) + " pushed=" + std::to_string(r.pushed) +
           " same=" + std::to_string(r.same) + " conflict=" + std::to_string(r.conflicts.size()) +
           " skip=" + std::to_string(r.skipped) + " cancel=" + (r.cancelled ? "1" : "0");
}

ConflictPolicy switchPolicy(int picked) {
    if (picked == 0) return ConflictPolicy::KeepWii;
    if (picked == 1) return ConflictPolicy::TakeCloud;
    return ConflictPolicy::Defer;
}

int parseSwitchAnswer(std::string_view v) {
    if (v == "wii") return 0;
    if (v == "cloud") return 1;
    if (v == "ask") return 2;
    return -1;
}

std::vector<SyncTarget> conflictTargets(std::span<const SyncTarget> targets, std::span<const std::string> ids) {
    const std::set<std::string, std::less<>> want(ids.begin(), ids.end());
    std::vector<SyncTarget> out;
    for (const SyncTarget& t : targets)
        if (want.count(t.unit.id) != 0) out.push_back(t);
    return out;
}

}  // namespace nl
