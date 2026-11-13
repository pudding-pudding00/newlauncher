#pragma once
// AES-128 (FIPS-197) und CBC (SP 800-38A), eigener Code nach der Normbeschreibung (M18 Task 9): entschlüsselt
// Wii-Partitionscluster mit dem Titelschlüssel. Keine Tabellen außer der S-Box; Geschwindigkeit reicht für
// FST + opening.bnr (unter 1 MiB). Der Common Key kommt hier nie vor: den Titelschlüssel liefert IOS (ES_Decrypt).

#include <array>
#include <span>

#include "core/types.hpp"

namespace nl {

using AesBlock = std::array<u8, 16>;

class Aes128 {
public:
    explicit Aes128(const AesBlock& key);
    void encryptBlock(const u8* in, u8* out) const;  // in == out erlaubt
    void decryptBlock(const u8* in, u8* out) const;  // in == out erlaubt

private:
    std::array<u8, 176> roundKeys_{};
};

// CBC über ganze Blöcke; in.size() == out.size(), Vielfaches von 16, sonst false. in == out erlaubt.
bool aesCbcDecrypt(const Aes128& aes, const AesBlock& iv, std::span<const u8> in, std::span<u8> out);
bool aesCbcEncrypt(const Aes128& aes, const AesBlock& iv, std::span<const u8> in, std::span<u8> out);

}  // namespace nl
