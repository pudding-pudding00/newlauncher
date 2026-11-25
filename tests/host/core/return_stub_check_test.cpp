// M20: PrÃ¼f-Logik des RÃ¼ckkehr-Stubs (reines C, lÃ¤uft im Stub und hier auf dem Host).
#include "launch/return_stub/stub_check.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void put32(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    b[at] = static_cast<unsigned char>(v >> 24);
    b[at + 1] = static_cast<unsigned char>(v >> 16);
    b[at + 2] = static_cast<unsigned char>(v >> 8);
    b[at + 3] = static_cast<unsigned char>(v);
}

// GÃ¼ltiger Block: 1000 Byte DOL (2 Sektoren) in zwei Extents, argv 5 Byte.
std::vector<unsigned char> validBlock() {
    std::vector<unsigned char> b(0x18 + 8 + 16, 0);
    put32(b, 0x00, NLRS_MAGIC);
    put32(b, 0x04, NLRS_VERSION);
    put32(b, 0x08, 1000);
    put32(b, 0x0C, 2);
    put32(b, 0x10, 5);
    std::memcpy(b.data() + 0x18, "abcd", 5);
    put32(b, 0x20, 100);  // lba
    put32(b, 0x24, 1);
    put32(b, 0x28, 300);
    put32(b, 0x2C, 1);
    put32(b, 0x14, nlrs_checksum(b.data() + 0x18, static_cast<std::uint32_t>(b.size() - 0x18)));
    return b;
}

void reseal(std::vector<unsigned char>& b) {
    put32(b, 0x14, nlrs_checksum(b.data() + 0x18, static_cast<std::uint32_t>(b.size() - 0x18)));
}

}  // namespace

TEST_CASE("nlrs_be32 liest big-endian") {
    const unsigned char p[4] = {0x12, 0x34, 0x56, 0x78};
    CHECK(nlrs_be32(p) == 0x12345678u);
}

TEST_CASE("nlrs_params_size: Kopf + aufgefÃ¼lltes argv + Extents") {
    const auto b = validBlock();
    CHECK(nlrs_params_size(b.data()) == b.size());
}

TEST_CASE("nlrs_params_ok: gÃ¼ltiger Block") {
    const auto b = validBlock();
    CHECK(nlrs_params_ok(b.data()) == 1);
}

TEST_CASE("nlrs_params_ok: Magic, Version, PrÃ¼fsumme") {
    auto b = validBlock();
    b[0] = 'X';
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x04, 2);
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    b[0x21] ^= 1;  // lba geÃ¤ndert, PrÃ¼fsumme nicht
    CHECK(nlrs_params_ok(b.data()) == 0);
}

TEST_CASE("nlrs_params_ok: Extents mÃ¼ssen genau die DOL-Sektoren abdecken") {
    auto b = validBlock();
    put32(b, 0x08, 1025);  // 3 Sektoren nÃ¶tig, 2 vorhanden
    reseal(b);
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x2C, 2);  // 3 Sektoren fÃ¼r 2 nÃ¶tige
    reseal(b);
    CHECK(nlrs_params_ok(b.data()) == 0);
}

TEST_CASE("nlrs_params_ok: Grenzen") {
    auto b = validBlock();
    put32(b, 0x0C, 0);  // keine Extents
    reseal(b);
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x10, NLRS_ARGV_MAX + 1);
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x0C, 1000);  // passt nicht in den Block
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x08, NLRS_MAX_DOL + 1);
    CHECK(nlrs_params_ok(b.data()) == 0);
    b = validBlock();
    put32(b, 0x24, 0);  // leeres Extent
    put32(b, 0x2C, 2);
    reseal(b);
    CHECK(nlrs_params_ok(b.data()) == 0);
}

namespace {

// DOL mit einer Textsektion (0x100 Byte bei 0x80004000), BSS und Einsprung.
std::vector<unsigned char> makeDol(std::uint32_t addr = 0x80004000, std::uint32_t entry = 0x80004000) {
    std::vector<unsigned char> d(0x100 + 0x100, 0);
    put32(d, 0x00, 0x100);   // offset[0]
    put32(d, 0x48, addr);    // address[0]
    put32(d, 0x90, 0x100);   // size[0]
    put32(d, 0xD8, 0x80005000);  // bss
    put32(d, 0xDC, 0x1000);
    put32(d, 0xE0, entry);
    return d;
}

}  // namespace

TEST_CASE("nlrs_dol_ok: gÃ¼ltige DOL") {
    const auto d = makeDol();
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 1);
}

TEST_CASE("nlrs_dol_ok: Sektion auÃŸerhalb der Datei") {
    auto d = makeDol();
    put32(d, 0x90, 0x101);
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol();
    put32(d, 0x00, 0xFFFFFFF0u);  // Ãœberlauf offset + size
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    CHECK(nlrs_dol_ok(d.data(), 0x40) == 0);  // kÃ¼rzer als der Kopf
}

TEST_CASE("nlrs_dol_ok: Ziele nie Ã¼ber Stub, Systemvariablen oder Lesepuffer") {
    auto d = makeDol(0x80001800);
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol(0x80003000);
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol(NLRS_BUFFER_ADDR - 0x80);  // ragt in den Lesepuffer
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol(0x817FFF80);  // ragt Ã¼ber das Ende von MEM1
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol(0x90000000);  // MEM2 unterhalb des Puffers ist erlaubt
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 1);
    d = makeDol();
    put32(d, 0xDC, 0x01800000);  // BSS bis Ã¼ber MEM1 hinaus
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
    d = makeDol(0x80004000, 0x80001800);  // Einsprung im Stub
    CHECK(nlrs_dol_ok(d.data(), static_cast<std::uint32_t>(d.size())) == 0);
}
