#include "core/pkce.hpp"

#include "core/base64.hpp"
#include "core/sha256.hpp"

namespace nl {

Pkce makePkce(std::span<const u8, 32> random) {
    Pkce p;
    p.verifier = base64Encode(random, true);
    const auto digest = sha256(std::span<const u8>(reinterpret_cast<const u8*>(p.verifier.data()), p.verifier.size()));
    p.challenge = base64Encode(digest, true);
    return p;
}

}  // namespace nl
