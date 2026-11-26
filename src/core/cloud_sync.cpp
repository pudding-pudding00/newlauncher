#include "core/cloud_sync.hpp"

namespace nl {

const char* syncActionName(SyncAction a) {
    switch (a) {
        case SyncAction::Nothing: return "nothing";
        case SyncAction::Download: return "download";
        case SyncAction::Upload: return "upload";
        case SyncAction::SetBase: return "setbase";
        case SyncAction::Conflict: return "conflict";
    }
    return "nothing";
}

SyncAction decideSync(const SyncInput& in) {
    const bool local = in.localHash.has_value();
    const bool remote = in.cloud.has_value();
    if (!local) return remote ? SyncAction::Download : SyncAction::Nothing;  // Löschungen nie übertragen
    if (!remote) return SyncAction::Upload;

    const std::string& lh = *in.localHash;
    const CloudEntry& c = *in.cloud;
    if (!in.base) return lh == c.hash ? SyncAction::SetBase : SyncAction::Conflict;

    const bool localNew = lh != in.base->hash;
    // Ein anderer Cloud-Hash zählt auch bei gleicher rev als neu (ohne Hash zählt nur die rev).
    const bool cloudNew = c.rev != in.base->rev || (!c.hash.empty() && c.hash != in.base->hash);
    if (!localNew && !cloudNew) return SyncAction::Nothing;
    if (!localNew) return c.hash == lh ? SyncAction::SetBase : SyncAction::Download;
    if (!cloudNew) return SyncAction::Upload;
    return c.hash == lh ? SyncAction::SetBase : SyncAction::Conflict;
}

}  // namespace nl
