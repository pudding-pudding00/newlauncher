#include "core/data_dir.hpp"

#include <vector>

namespace nl {
namespace {

constexpr std::string_view kDirName = "/newlauncher";

bool isUsb(const DataCandidate& d) { return d.device.rfind("usb", 0) == 0; }

const DataCandidate* find(std::span<const DataCandidate> devs, std::string_view name) {
    if (name.empty()) return nullptr;
    for (const DataCandidate& d : devs)
        if (d.device == name) return &d;
    return nullptr;
}

DataDirChoice at(const DataCandidate& d, bool create, bool readOnly) {
    return DataDirChoice{d.device + ":" + std::string(kDirName), create, readOnly};
}

}  // namespace

std::string deviceFromArgv0(std::string_view argv0) {
    const auto colon = argv0.find(':');
    if (colon == std::string_view::npos || colon == 0) return {};
    std::string out;
    for (char c : argv0.substr(0, colon)) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return {};
        out += c;
    }
    return out;
}

DataDirChoice chooseDataDir(std::string_view argv0, std::span<const DataCandidate> devs) {
    // Reihenfolge der Kandidaten: Startgerät, sd, dann USB-Geräte in Tabellenreihenfolge.
    std::vector<const DataCandidate*> order;
    auto push = [&](const DataCandidate* d) {
        if (d == nullptr) return;
        for (const DataCandidate* o : order)
            if (o == d) return;
        order.push_back(d);
    };
    push(find(devs, deviceFromArgv0(argv0)));
    push(find(devs, "sd"));
    for (const DataCandidate& d : devs)
        if (isUsb(d)) push(&d);

    for (const DataCandidate* d : order)
        if (d->writable && d->hasDataDir) return at(*d, false, false);
    for (const DataCandidate* d : order)
        if (d->writable) return at(*d, true, false);
    for (const DataCandidate* d : order)
        if (d->hasDataDir) return at(*d, false, true);
    return DataDirChoice{"", false, true};
}

bool wantUsbForData(std::string_view argv0, std::span<const DataCandidate> devs) {
    bool anyUsb = false;
    bool anyDir = false;
    for (const DataCandidate& d : devs) {
        anyUsb = anyUsb || isUsb(d);
        anyDir = anyDir || d.hasDataDir;
    }
    const std::string boot = deviceFromArgv0(argv0);
    if (boot.rfind("usb", 0) == 0 && find(devs, boot) == nullptr) return true;
    return !anyDir && !anyUsb;
}

}  // namespace nl
