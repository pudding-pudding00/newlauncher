#pragma once
// Geräteschlüssel und Zufall für den Token-Speicher (M27, Spec Ziel 8).

#include <span>
#include <vector>

#include "core/types.hpp"

namespace nl::platform {

// ES-Geräte-ID (4 Bytes, big endian) plus fester Kontext. In Dolphin fest. Verschleierung, kein Geheimnis.
std::vector<u8> deviceKeyMaterial();

// CSPRNG von wii_tls (psa_generate_random). Nur im Netz-Thread nach wtls_init; ohne NL_TLS immer false.
bool secureRandom(std::span<u8> out);

// secureRandom, sonst randomBytes (Zeitgeber-Jitter) – für Salt und IV reicht Eindeutigkeit.
void saltRandom(std::span<u8> out);

}  // namespace nl::platform
