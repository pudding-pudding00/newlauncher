#include "core/forwarder_plan.hpp"

namespace nl {

std::vector<std::string> forwarderCandidates() {
    return {"sd:/apps/newlauncher/boot.dol", "usb:/apps/newlauncher/boot.dol"};
}

std::vector<std::string> forwarderArgv(std::string_view dolPath) {
    return {std::string(dolPath), std::string(kForwarderFlag)};
}

bool hasForwarderFlag(int argc, const char* const* argv) {
    if (argv == nullptr) return false;
    for (int i = 0; i < argc; ++i) {
        if (argv[i] != nullptr && std::string_view(argv[i]) == kForwarderFlag) {
            return true;
        }
    }
    return false;
}

}  // namespace nl
