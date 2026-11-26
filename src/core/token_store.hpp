#pragma once
// Token-Speicher <data>/cloud/tokens.bin (M27, Spec Ziel 8). AES-128-CBC + HMAC-SHA256 (encrypt-then-MAC) mit einem
// Schlüssel aus Geräte-ID und Salt. Das ist Verschleierung gegen Mitlesen der SD-Karte, kein Schutz gegen jemanden,
// der die Wii selbst hat; den Schutz geben enge Scopes und Abmelden.
//
// Format: "NLTK", Version 1, salt[16], iv[16], Länge u32 BE, Chiffrat (PKCS#7), HMAC[32] über alles davor.
// k = HMAC(salt, deviceId); encKey = HMAC(k, "enc")[0..16); macKey = HMAC(k, "mac").

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/oauth_device.hpp"
#include "core/types.hpp"

namespace nl {

struct StoredTokens {
    std::string provider, account;
    TokenSet tokens;  // expiresAtMs wird nicht gespeichert (monotone Uhr gilt je Sitzung) → nach dem Laden 0
};

std::vector<u8> sealTokens(const StoredTokens& t, std::span<const u8> deviceId, std::span<const u8, 16> salt,
                           std::span<const u8, 16> iv);
std::optional<StoredTokens> unsealTokens(std::span<const u8> blob, std::span<const u8> deviceId);

// Für Logs: "<geheim:N>" mit N = Länge, nie ein Zeichen aus s.
std::string redactSecret(std::string_view s);

}  // namespace nl
