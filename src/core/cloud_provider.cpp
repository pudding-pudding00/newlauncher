#include "core/cloud_provider.hpp"

namespace nl {

const char* cloudStatusName(CloudStatus s) {
    switch (s) {
        case CloudStatus::Ok: return "ok";
        case CloudStatus::NotFound: return "notfound";
        case CloudStatus::RevMismatch: return "revmismatch";
        case CloudStatus::Unauthorized: return "auth";
        case CloudStatus::Offline: return "offline";
        case CloudStatus::Failed: return "error";
    }
    return "error";
}

bool cloudSafeUnitId(std::string_view unit) {
    if (unit.empty() || unit.size() > 64 || unit.front() == '.') return false;
    for (const char c : unit) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

}  // namespace nl
