#include "platform/device_key.hpp"

#include <gccore.h>

#ifdef NL_TLS
#include <psa/crypto.h>
#endif

#include "platform/system.hpp"

namespace nl::platform {

std::vector<u8> deviceKeyMaterial() {
    u32 device = 0;
    if (ES_GetDeviceID(&device) < 0) device = 0;
    std::vector<u8> out = {static_cast<u8>(device >> 24), static_cast<u8>(device >> 16),
                           static_cast<u8>(device >> 8), static_cast<u8>(device)};
    constexpr char kContext[] = "newlauncher-cloud-v1";
    out.insert(out.end(), kContext, kContext + sizeof kContext - 1);
    return out;
}

bool secureRandom(std::span<u8> out) {
#ifdef NL_TLS
    return psa_generate_random(out.data(), out.size()) == PSA_SUCCESS;
#else
    (void)out;
    return false;
#endif
}

void saltRandom(std::span<u8> out) {
    if (!secureRandom(out)) randomBytes(out);
}

}  // namespace nl::platform
