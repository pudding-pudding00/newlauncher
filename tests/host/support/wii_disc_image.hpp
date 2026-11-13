#pragma once
// Baut verschlüsselte Wii-Abbilder für die Banner-Tests (M18 Task 9). Alle Schlüssel sind frei gewählte
// Testwerte; der Titelschlüssel wird mit einem Test-„Gemeinschaftsschlüssel“ verschlüsselt, den nur die Tests
// kennen. Layout nach WiiBrew „Wii Disc“: Partitionstabelle 0x40000, Ticket am Partitionsanfang,
// Daten-Offset 0x2B8 (×4), Cluster 0x8000 = 0x400 Hash-Bereich (IV bei 0x3D0) + 0x7C00 Daten.

#include <array>
#include <cstring>
#include <vector>

#include "core/aes128.hpp"
#include "core/types.hpp"
#include "support/disc_images.hpp"
#include "support/gc_disc_images.hpp"

namespace nl::test {

inline constexpr AesBlock kTestCommonKey = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                                            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
inline constexpr AesBlock kTestTitleKey = {0xA5, 0x5A, 0x3C, 0xC3, 0x0F, 0xF0, 0x96, 0x69,
                                           0x12, 0x21, 0x34, 0x43, 0x56, 0x65, 0x78, 0x87};

struct WiiDiscSpec {
    std::vector<FstSpec> root;
    u8 keyIndex = 0;
    bool updatePartition = true;  // Typ-1-Partition vor der Spielpartition
    bool gamePartition = true;
};

inline constexpr u32 kTestUpdatePart = 0x50000;
inline constexpr u32 kTestGamePart = 0x60000;
inline constexpr u32 kTestDataOff = 0x20000;  // relativ zur Partition

// Entschlüsselter Partitionsinhalt: Disc-Kopf, FST bei 0x2000 (Wii: Offsets und Größen ×4 gespeichert).
inline std::vector<u8> wiiPartitionPlain(const std::vector<FstSpec>& root) {
    constexpr u32 kFst = 0x2000;
    std::vector<u8> plain = wiiIso("RTSE01", "Testspiel", kFst);
    const std::vector<u8> probe = buildFst(root, 0);
    const u32 dataStart = (kFst + static_cast<u32>(probe.size()) + 31u) & ~31u;
    std::vector<u8> fst = buildFst(root, dataStart, &plain);
    // Dateioffsets durch 4 (alle 32-Byte-ausgerichtet); Ordner-Einträge unverändert.
    const u32 count = (static_cast<u32>(fst[8]) << 24) | (static_cast<u32>(fst[9]) << 16) | (static_cast<u32>(fst[10]) << 8) | fst[11];
    for (u32 i = 1; i < count; ++i) {
        const std::size_t at = std::size_t{i} * 12;
        if (fst[at] != 0) continue;
        const u32 off = (static_cast<u32>(fst[at + 4]) << 24) | (static_cast<u32>(fst[at + 5]) << 16) |
                        (static_cast<u32>(fst[at + 6]) << 8) | fst[at + 7];
        putBe32(fst, at + 4, off >> 2);
    }
    if (plain.size() < kFst + fst.size()) plain.resize(kFst + fst.size(), 0);
    std::memcpy(plain.data() + kFst, fst.data(), fst.size());
    putBe32(plain, 0x424, kFst >> 2);
    putBe32(plain, 0x428, static_cast<u32>((fst.size() + 3) / 4));
    return plain;
}

inline std::vector<u8> wiiDiscImage(const WiiDiscSpec& spec) {
    std::vector<u8> img = wiiIso("RTSE01", "Testspiel", 0x40000);
    std::vector<std::array<u32, 2>> parts;
    if (spec.updatePartition) parts.push_back({kTestUpdatePart, 1});
    if (spec.gamePartition) parts.push_back({kTestGamePart, 0});
    img.resize(0x40040, 0);
    putBe32(img, 0x40000, static_cast<u32>(parts.size()));
    putBe32(img, 0x40004, 0x40020 >> 2);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        putBe32(img, 0x40020 + i * 8, parts[i][0] >> 2);
        putBe32(img, 0x40020 + i * 8 + 4, parts[i][1]);
    }

    // Update-Partition: nur Rauschen, darf nie gelesen werden müssen.
    if (spec.updatePartition) {
        img.resize(kTestUpdatePart + 0x400, 0);
        for (u32 i = 0; i < 0x400; ++i) img[kTestUpdatePart + i] = static_cast<u8>(i * 7 + 3);
    }
    if (!spec.gamePartition) return img;

    // Ticket: Titel-ID, Schlüsselindex, Titelschlüssel CBC-verschlüsselt (IV = Titel-ID + 8 Nullen).
    const u32 p = kTestGamePart;
    img.resize(p + kTestDataOff, 0);
    const u8 titleId[8] = {0x00, 0x01, 0x00, 0x00, 'R', 'T', 'S', 'E'};
    std::memcpy(img.data() + p + 0x1DC, titleId, 8);
    img[p + 0x1F1] = spec.keyIndex;
    AesBlock iv{};
    std::memcpy(iv.data(), titleId, 8);
    AesBlock encKey{};
    aesCbcEncrypt(Aes128(kTestCommonKey), iv, kTestTitleKey, encKey);
    std::memcpy(img.data() + p + 0x1BF, encKey.data(), 16);

    // Daten: in 0x7C00-Stücke, je Cluster eigener IV im Hash-Bereich.
    std::vector<u8> plain = wiiPartitionPlain(spec.root);
    plain.resize((plain.size() + 0x7BFF) / 0x7C00 * 0x7C00, 0);
    const std::size_t clusters = plain.size() / 0x7C00;
    putBe32(img, p + 0x2B8, kTestDataOff >> 2);
    putBe32(img, p + 0x2BC, static_cast<u32>(clusters * 0x8000 >> 2));
    const Aes128 title(kTestTitleKey);
    for (std::size_t c = 0; c < clusters; ++c) {
        std::vector<u8> cl(0x8000, 0);
        for (u32 i = 0; i < 0x400; ++i) cl[i] = static_cast<u8>(c * 31 + i * 13 + 1);  // Hash-Bereich: Testmuster
        AesBlock civ{};
        std::memcpy(civ.data(), cl.data() + 0x3D0, 16);
        aesCbcEncrypt(title, civ, std::span<const u8>(plain.data() + c * 0x7C00, 0x7C00), std::span<u8>(cl.data() + 0x400, 0x7C00));
        img.insert(img.end(), cl.begin(), cl.end());
    }
    return img;
}

// .wbfs-Datei (hd-Sektor 512, WBFS-Block 2^shift) mit Slot 0 = disc. order: Disc-Block i → Dateiblock order[i]
// (0 = nicht belegt, Block muss dann nur Nullen enthalten).
inline std::vector<u8> wbfsFileFromDisc(const std::vector<u8>& disc, u8 shift, const std::vector<u16>& order) {
    const std::size_t block = std::size_t{1} << shift;
    u16 maxBlock = 0;
    for (u16 b : order) maxBlock = b > maxBlock ? b : maxBlock;
    std::vector<u8> f((std::size_t{maxBlock} + 1) * block, 0);
    std::memcpy(f.data(), "WBFS", 4);
    putBe32(f, 4, static_cast<u32>(f.size() / 512));
    f[8] = 9;
    f[9] = shift;
    f[12] = 1;
    std::memcpy(f.data() + 512, disc.data(), 0x100);
    for (std::size_t i = 0; i < order.size(); ++i) {
        f[512 + 0x100 + 2 * i] = static_cast<u8>(order[i] >> 8);
        f[512 + 0x100 + 2 * i + 1] = static_cast<u8>(order[i]);
        if (order[i] == 0) continue;
        const std::size_t from = i * block;
        if (from >= disc.size()) continue;
        const std::size_t n = std::min(block, disc.size() - from);
        std::memcpy(f.data() + std::size_t{order[i]} * block, disc.data() + from, n);
    }
    return f;
}

}  // namespace nl::test
