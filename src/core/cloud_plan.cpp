#include "core/cloud_plan.hpp"

#include <optional>
#include <utility>

#include "core/cloud_state.hpp"
#include "core/cloud_sync.hpp"
#include "core/save_archive.hpp"
#include "core/save_backup.hpp"

namespace nl {

namespace {

std::string statePath(const CloudCtx& ctx) { return ctx.dataDir + "/cloud/state.ini"; }

CloudState loadState(CloudCtx& ctx) {
    auto r = ctx.fs.readAll(statePath(ctx), 1024 * 1024);
    if (!r.isOk()) return CloudState{};
    const auto& b = r.value();
    return CloudState::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
}

bool saveState(CloudCtx& ctx, const CloudState& s) {
    if (!ctx.fs.makeDirs(ctx.dataDir + "/cloud").isOk()) return false;
    const std::string text = s.serialize();
    return ctx.fs.writeAtomic(statePath(ctx), {reinterpret_cast<const u8*>(text.data()), text.size()}).isOk();
}

void log(CloudCtx& ctx, std::string_view unit, std::string_view what) {
    if (ctx.log) ctx.log("cloud " + std::string(unit) + " " + std::string(what));
}

const char* skipReason(CloudStatus s) {
    switch (s) {
        case CloudStatus::Offline: return "offline";
        case CloudStatus::Unauthorized: return "auth";
        default: return "error";
    }
}

PreLaunchResult skipped(CloudCtx& ctx, std::string_view unit, std::string reason) {
    log(ctx, unit, "skip " + reason);
    PreLaunchResult r;
    r.kind = PreLaunch::Skipped;
    r.reason = std::move(reason);
    return r;
}

struct Local {
    std::vector<SaveFile> files;
    std::optional<std::string> hash;  // nullopt = leer
};

std::optional<Local> readLocal(CloudCtx& ctx, const SaveUnit& unit) {
    auto r = readUnit(ctx.fs, unit);
    if (!r.isOk()) return std::nullopt;
    Local l;
    l.files = std::move(r.value());
    if (!l.files.empty()) l.hash = contentHash(l.files);
    return l;
}

// stat → optional<CloudEntry>; andere Fehler über status.
CloudStatus statCloud(CloudCtx& ctx, const SaveUnit& unit, std::optional<CloudEntry>& out,
                      const volatile bool& cancel) {
    CloudEntry e;
    const CloudStatus s = ctx.provider.stat(unit.id, e, cancel);
    if (s == CloudStatus::NotFound) {
        out.reset();
        return CloudStatus::Ok;
    }
    if (s == CloudStatus::Ok) out = std::move(e);
    return s;
}

// Herunterladen, prüfen (Einheit, Hash gegen die Cloud-Eigenschaft), erst dann schreiben.
PreLaunchResult pull(CloudCtx& ctx, const SaveUnit& unit, std::string_view title, const CloudEntry& cloud,
                     CloudState& state, const volatile bool& cancel) {
    std::vector<u8> zip;
    const CloudStatus s = ctx.provider.download(cloud, zip, cancel);
    if (s != CloudStatus::Ok) return skipped(ctx, unit.id, skipReason(s));
    auto pkg = unpackSave(zip);
    if (!pkg.isOk()) return skipped(ctx, unit.id, "error");
    if (pkg.value().meta.unit != unit.id || pkg.value().meta.hash != cloud.hash ||
        contentHash(pkg.value().files) != cloud.hash) {
        return skipped(ctx, unit.id, "error");
    }
    if (!restoreUnit(ctx.fs, ctx.dataDir, unit, pkg.value(), title, ctx.device, ctx.now).isOk()) {
        return skipped(ctx, unit.id, "nand");
    }
    state.setBase(ctx.account, unit.id, SyncBase{cloud.hash, cloud.rev});
    saveState(ctx, state);  // scheitert das, entscheidet der nächste Lauf mit gleichem Hash → nur Basis setzen
    log(ctx, unit.id, "pull ok");
    PreLaunchResult r;
    r.kind = PreLaunch::Pulled;
    r.cloud = cloud;
    return r;
}

// Hochladen mit erwarteter Revision; bei Ok Basis setzen.
CloudStatus push(CloudCtx& ctx, const SaveUnit& unit, std::string_view title, const Local& local,
                 std::string_view expectRev, CloudState& state, CloudEntry& out, const volatile bool& cancel) {
    SaveMeta meta;
    meta.unit = unit.id;
    meta.title = std::string(title);
    meta.device = ctx.device;
    meta.saved = ctx.now;
    auto zip = packSave(meta, local.files);
    if (!zip.isOk()) return CloudStatus::Failed;
    meta.hash = *local.hash;
    meta.files = static_cast<u32>(local.files.size());
    const CloudStatus s = ctx.provider.upload(unit.id, zip.value(), meta, expectRev, out, cancel);
    if (s != CloudStatus::Ok) {
        log(ctx, unit.id, std::string("push ") + cloudStatusName(s));
        return s;
    }
    state.setBase(ctx.account, unit.id, SyncBase{*local.hash, out.rev});
    saveState(ctx, state);
    log(ctx, unit.id, "push ok");
    return CloudStatus::Ok;
}

PreLaunchResult conflict(CloudCtx& ctx, const SaveUnit& unit, const CloudEntry& cloud, const CloudState& state) {
    log(ctx, unit.id, "conflict");
    PreLaunchResult r;
    r.kind = PreLaunch::Conflict;
    r.cloud = cloud;
    r.localPlayed = state.played(ctx.account, unit.id);
    return r;
}

}  // namespace

PreLaunchResult preLaunchSync(CloudCtx& ctx, const SaveUnit& unit, std::string_view title,
                              const volatile bool& cancel) {
    const auto local = readLocal(ctx, unit);
    if (!local) return skipped(ctx, unit.id, "nand");
    CloudState state = loadState(ctx);

    // Höchstens zwei Runden: ändert sich die Cloud zwischen stat und upload zweimal, fragt der Nutzer.
    std::optional<CloudEntry> cloud;
    for (int round = 0; round < 2; ++round) {
        if (const CloudStatus s = statCloud(ctx, unit, cloud, cancel); s != CloudStatus::Ok) {
            return skipped(ctx, unit.id, skipReason(s));
        }
        const SyncInput in{local->hash, state.base(ctx.account, unit.id), cloud};
        switch (decideSync(in)) {
            case SyncAction::Nothing: {
                log(ctx, unit.id, "noop");
                return PreLaunchResult{PreLaunch::Ready, {}, 0, {}};
            }
            case SyncAction::SetBase: {
                state.setBase(ctx.account, unit.id, SyncBase{cloud->hash, cloud->rev});
                saveState(ctx, state);
                log(ctx, unit.id, "setbase");
                return PreLaunchResult{PreLaunch::Ready, *cloud, 0, {}};
            }
            case SyncAction::Download: return pull(ctx, unit, title, *cloud, state, cancel);
            case SyncAction::Conflict: return conflict(ctx, unit, *cloud, state);
            case SyncAction::Upload: {
                CloudEntry out;
                const CloudStatus s =
                    push(ctx, unit, title, *local, cloud ? std::string_view(cloud->rev) : std::string_view(), state,
                         out, cancel);
                if (s == CloudStatus::Ok) return PreLaunchResult{PreLaunch::Pushed, out, 0, {}};
                if (s != CloudStatus::RevMismatch) return skipped(ctx, unit.id, skipReason(s));
                break;  // neu entscheiden
            }
        }
    }
    if (!cloud) return skipped(ctx, unit.id, "error");
    return conflict(ctx, unit, *cloud, state);
}

PreLaunchResult resolveConflict(CloudCtx& ctx, const SaveUnit& unit, std::string_view title, ConflictChoice choice,
                                const volatile bool& cancel) {
    CloudState state = loadState(ctx);
    std::optional<CloudEntry> cloud;
    if (const CloudStatus s = statCloud(ctx, unit, cloud, cancel); s != CloudStatus::Ok) {
        return skipped(ctx, unit.id, skipReason(s));
    }
    if (choice == ConflictChoice::TakeCloud) {
        if (!cloud) return skipped(ctx, unit.id, "error");
        return pull(ctx, unit, title, *cloud, state, cancel);
    }
    const auto local = readLocal(ctx, unit);
    if (!local) return skipped(ctx, unit.id, "nand");
    if (!local->hash) return skipped(ctx, unit.id, "error");  // leer hochladen wäre eine übertragene Löschung
    CloudEntry out;
    const CloudStatus s = push(ctx, unit, title, *local, cloud ? std::string_view(cloud->rev) : std::string_view(),
                               state, out, cancel);
    if (s == CloudStatus::Ok) return PreLaunchResult{PreLaunch::Pushed, out, 0, {}};
    if (s == CloudStatus::RevMismatch) {
        std::optional<CloudEntry> again;
        if (statCloud(ctx, unit, again, cancel) == CloudStatus::Ok && again) return conflict(ctx, unit, *again, state);
        return skipped(ctx, unit.id, "error");
    }
    return skipped(ctx, unit.id, skipReason(s));
}

CloudStatus uploadIfChanged(CloudCtx& ctx, const SaveUnit& unit, std::string_view title,
                            const volatile bool& cancel) {
    const auto local = readLocal(ctx, unit);
    if (!local) return CloudStatus::Failed;
    if (!local->hash) return CloudStatus::Ok;  // nichts zu sichern
    CloudState state = loadState(ctx);
    const auto base = state.base(ctx.account, unit.id);
    if (base && base->hash == *local->hash) return CloudStatus::Ok;  // unverändert, kein Netz

    std::optional<CloudEntry> cloud;
    for (int round = 0; round < 2; ++round) {
        if (const CloudStatus s = statCloud(ctx, unit, cloud, cancel); s != CloudStatus::Ok) return s;
        switch (decideSync(SyncInput{local->hash, state.base(ctx.account, unit.id), cloud})) {
            case SyncAction::Nothing:
            case SyncAction::Download: return CloudStatus::Ok;  // das Start-Tor holt den neueren Stand
            case SyncAction::SetBase:
                state.setBase(ctx.account, unit.id, SyncBase{cloud->hash, cloud->rev});
                saveState(ctx, state);
                return CloudStatus::Ok;
            case SyncAction::Conflict: log(ctx, unit.id, "conflict"); return CloudStatus::RevMismatch;
            case SyncAction::Upload: {
                CloudEntry out;
                const CloudStatus s =
                    push(ctx, unit, title, *local, cloud ? std::string_view(cloud->rev) : std::string_view(), state,
                         out, cancel);
                if (s != CloudStatus::RevMismatch) return s;
                break;
            }
        }
    }
    return CloudStatus::RevMismatch;
}

void notePlayed(CloudCtx& ctx, std::string_view unit, u64 unixTime) {
    CloudState state = loadState(ctx);
    state.setPlayed(ctx.account, unit, unixTime);
    saveState(ctx, state);
}

}  // namespace nl
