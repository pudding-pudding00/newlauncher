#include <doctest/doctest.h>

#include "core/bin_io.hpp"
#include "core/dol.hpp"
#include "core/elf.hpp"

using namespace nl;

namespace {

struct TestPhdr {
    u32 type = 1;  // PT_LOAD
    u32 offset = 0;
    u32 vaddr = 0;
    u32 paddr = 0;
    u32 filesz = 0;
    u32 memsz = 0;
    u32 flags = 5;  // PF_R | PF_X
    u32 align = 32;
};

std::vector<u8> makeTestElf(u32 entry, const std::vector<TestPhdr>& phdrs,
                            const std::vector<u8>& payload, u8 cls = 1, u8 endian = 2,
                            u16 type = 2, u16 machine = 20) {
    BeWriter w;
    w.u8(0x7F); w.u8('E'); w.u8('L'); w.u8('F');
    w.u8(cls); w.u8(endian); w.u8(1);
    for (int i = 0; i < 9; ++i) w.u8(0);
    w.u16(type);
    w.u16(machine);
    w.u32(1);
    w.u32(entry);
    w.u32(52);  // e_phoff
    w.u32(0);   // e_shoff
    w.u32(0);   // e_flags
    w.u16(52);  // e_ehsize
    w.u16(32);  // e_phentsize
    w.u16(static_cast<u16>(phdrs.size()));
    w.u16(0); w.u16(0); w.u16(0);
    for (const auto& p : phdrs) {
        w.u32(p.type);
        w.u32(p.offset);
        w.u32(p.vaddr);
        w.u32(p.paddr);
        w.u32(p.filesz);
        w.u32(p.memsz);
        w.u32(p.flags);
        w.u32(p.align);
    }
    auto out = w.bytes();
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace

TEST_CASE("elfToDol converts valid 32-bit BE PPC ELF with text, data, and BSS") {
    std::vector<u8> payload(64, 0xAA);
    payload[0] = 0x4E; payload[1] = 0x80; payload[2] = 0x00; payload[3] = 0x20;  // blr
    const u32 dataOff = 52 + 2 * 32;
    std::vector<TestPhdr> phdrs = {
        {1, dataOff, 0x00004000u, 0x00004000u, 32, 32, 5, 32},         // physical -> 0x80004000 text
        {1, dataOff + 32, 0x80005000u, 0x80005000u, 32, 96, 6, 32},    // data + 64B BSS
    };
    auto elf = makeTestElf(0x00004000u, phdrs, payload);
    CHECK(isElfMagic(elf));

    auto dol = elfToDol(elf);
    REQUIRE(dol.isOk());
    auto hdr = checkDolImage(dol.value(), kMaxDolBytes);
    REQUIRE(hdr.isOk());
    CHECK(hdr.value().entry == 0x80004000u);
    CHECK(hdr.value().sections[0].address == 0x80004000u);
    CHECK(hdr.value().sections[0].size == 32u);
    CHECK(hdr.value().sections[7].address == 0x80005000u);
    CHECK(hdr.value().sections[7].size == 32u);
    CHECK(hdr.value().bssAddress == 0x80005020u);
    CHECK(hdr.value().bssSize == 64u);
}

TEST_CASE("elfToDol rejects invalid architecture, endianness, overlaps, and reserved MEM2") {
    std::vector<u8> payload(32, 0x11);
    const u32 dataOff = 52 + 32;
    std::vector<TestPhdr> one = {{1, dataOff, 0x80004000u, 0x80004000u, 32, 32, 5, 32}};

    CHECK_FALSE(elfToDol(makeTestElf(0x80004000u, one, payload, 2, 2, 2, 20)).isOk());  // 64-bit
    CHECK_FALSE(elfToDol(makeTestElf(0x80004000u, one, payload, 1, 1, 2, 20)).isOk());  // little-endian
    CHECK_FALSE(elfToDol(makeTestElf(0x80004000u, one, payload, 1, 2, 2, 3)).isOk());   // x86
    CHECK_FALSE(elfToDol(makeTestElf(0x80004000u, one, payload, 1, 2, 3, 20)).isOk());  // ET_DYN

    // Overlapping segments
    const u32 twoOff = 52 + 2 * 32;
    std::vector<TestPhdr> overlap = {
        {1, twoOff, 0x80004000u, 0x80004000u, 32, 64, 5, 32},
        {1, twoOff, 0x80004020u, 0x80004020u, 32, 32, 6, 32},
    };
    CHECK_FALSE(elfToDol(makeTestElf(0x80004000u, overlap, payload)).isOk());

    // Overlaps reserved chainloader buffer
    std::vector<TestPhdr> reserved = {{1, dataOff, kDolBufferAddr, kDolBufferAddr, 32, 32, 5, 32}};
    CHECK_FALSE(elfToDol(makeTestElf(kDolBufferAddr, reserved, payload)).isOk());
}
