#pragma once
// Entscheidung je Einheit nach der Tabelle in Spec Ziel 5 (M27). Rein funktional, kein I/O.

#include <optional>
#include <string>

#include "core/cloud_provider.hpp"

namespace nl {

enum class SyncAction : u8 { Nothing, Download, Upload, SetBase, Conflict };

const char* syncActionName(SyncAction a);  // "nothing", "download", …

struct SyncBase {
    std::string hash, rev;  // letzter gemeinsamer Stand
};

struct SyncInput {
    std::optional<std::string> localHash;  // nullopt = Einheit lokal leer
    std::optional<SyncBase> base;
    std::optional<CloudEntry> cloud;        // nullopt = nicht in der Cloud
};

SyncAction decideSync(const SyncInput& in);

}  // namespace nl
