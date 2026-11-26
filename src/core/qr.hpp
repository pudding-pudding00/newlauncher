#pragma once
// QR-Code-Erzeuger (M29, Kopplungsseite und Dropbox-Link als QR). Nur Byte-Modus, Fehlerkorrektur M, Versionen
// 1–10 (bis 213 Bytes) – genug für eine URL. Die Maske wird nach den Strafpunkten aus ISO/IEC 18004 gewählt.

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

struct QrCode {
    int size = 0;              // 21 + 4·(Version − 1)
    std::vector<u8> modules;   // zeilenweise, size·size, 1 = dunkel; ohne Ruhezone
    bool dark(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size && modules[static_cast<std::size_t>(y * size + x)] != 0;
    }
};

// Kleinste Version 1–10 mit ECC M, sonst nullopt (zu lang).
std::optional<QrCode> encodeQr(std::string_view bytes);

// Bausteine, für Tests offen.
std::vector<u8> qrRsEcc(std::span<const u8> data, int ecLen);  // Reed-Solomon über GF(256)/0x11D
u16 qrFormatWord(int mask);                                    // ECC M, BCH(15,5), XOR 0x5412
u32 qrVersionWord(int version);                                // BCH(18,6)

}  // namespace nl
