#pragma once
// PKCE nach RFC 7636 (M29, Dropbox-Anmeldung ohne Secret). Der Zufall kommt von außen (CSPRNG im Netz-Thread);
// der Verifier verlässt die Wii nie, nur die Challenge steht in der Autorisierungs-URL.

#include <span>
#include <string>

#include "core/types.hpp"

namespace nl {

struct Pkce {
    std::string verifier;   // base64url(32 Bytes) = 43 Zeichen
    std::string challenge;  // base64url(SHA-256(verifier)), Methode S256
};

Pkce makePkce(std::span<const u8, 32> random);

}  // namespace nl
