#pragma once
// SHA-256 nach FIPS 180-4, eigene Umsetzung (M17, PIN der Kindersicherung). Kein Fremdcode.

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace nl {

class Sha256 {
public:
    Sha256();
    void update(std::span<const u8> data);
    std::array<u8, 32> finish();  // danach nicht weiterverwenden

private:
    void block(const u8* p);
    std::array<u32, 8> h_;
    std::array<u8, 64> buf_{};
    std::size_t used_ = 0;
    u64 bytes_ = 0;
};

std::array<u8, 32> sha256(std::span<const u8> data);

std::string bytesToHex(std::span<const u8> bytes);  // Kleinbuchstaben
// Genau 2 * out.size() Hexziffern (Groß/klein egal); sonst false und out unverändert.
bool hexToBytes(std::string_view hex, std::span<u8> out);

}  // namespace nl
