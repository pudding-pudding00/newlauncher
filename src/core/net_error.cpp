#include "core/net_error.hpp"

namespace nl {

NetError netErrorFromTls(int status) {
    using namespace wtls_status;
    switch (status) {
        case kOk: return NetError::None;
        case kDns: return NetError::Dns;
        case kConnect: return NetError::Connect;
        case kTimeout: return NetError::Timeout;
        case kCert: return NetError::Cert;
        case kClock: return NetError::Clock;
        case kHandshake: return NetError::Handshake;
        case kNoMem: return NetError::NoMem;
        default: return NetError::Other;
    }
}

bool tlsClockSuspect(int status, u32 verifyFlags) {
    if (status == wtls_status::kClock) return true;
    return status == wtls_status::kCert && (verifyFlags & (kX509BadCertExpired | kX509BadCertFuture)) != 0;
}

}  // namespace nl
