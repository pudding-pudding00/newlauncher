#include <doctest/doctest.h>

#include "core/bin_io.hpp"
#include "core/deflate.hpp"
#include "core/wiiload_proto.hpp"

using namespace nl;

namespace {

std::vector<u8> makeHeader(u8 major, u8 minor, u16 argsLen, u32 compSize, u32 uncompSize) {
    BeWriter w;
    w.u8('H');
    w.u8('A');
    w.u8('X');
    w.u8('X');
    w.u8(major);
    w.u8(minor);
    w.u16(argsLen);
    w.u32(compSize);
    w.u32(uncompSize);
    return w.bytes();
}

u32 testAdler32(std::span<const u8> data) {
    u32 a = 1, b = 0;
    for (u8 c : data) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::vector<u8> makeZlib(std::span<const u8> plain) {
    auto def = deflateRaw(plain);
    BeWriter w;
    w.u8(0x78);
    w.u8(0x9C);
    std::vector<u8> out = w.bytes();
    out.insert(out.end(), def.begin(), def.end());
    u32 a = testAdler32(plain);
    out.push_back(static_cast<u8>(a >> 24));
    out.push_back(static_cast<u8>(a >> 16));
    out.push_back(static_cast<u8>(a >> 8));
    out.push_back(static_cast<u8>(a));
    return out;
}

}  // namespace

TEST_CASE("parseWiiloadHeader accepts valid v0.5 and v0.4 headers and rejects invalid ones") {
    auto h5 = parseWiiloadHeader(makeHeader(0, 5, 12, 1024, 2048));
    REQUIRE(h5.isOk());
    CHECK(h5.value().major == 0);
    CHECK(h5.value().minor == 5);
    CHECK(h5.value().argsLen == 12);
    CHECK(h5.value().compSize == 1024);
    CHECK(h5.value().uncompSize == 2048);

    auto h4 = parseWiiloadHeader(makeHeader(0, 4, 0, 512, 0));
    REQUIRE(h4.isOk());

    auto badMagic = makeHeader(0, 5, 0, 100, 0);
    badMagic[0] = 'X';
    CHECK_FALSE(parseWiiloadHeader(badMagic).isOk());
    CHECK_FALSE(parseWiiloadHeader(makeHeader(1, 0, 0, 100, 0)).isOk());
    CHECK_FALSE(parseWiiloadHeader(makeHeader(0, 5, 0, 0, 0)).isOk());
    CHECK_FALSE(parseWiiloadHeader(makeHeader(0, 5, 0, kWiiloadMaxCompBytes + 1, 0)).isOk());
    CHECK_FALSE(parseWiiloadHeader(makeHeader(0, 5, 0, 100, kWiiloadMaxUncompBytes + 1)).isOk());
    CHECK_FALSE(parseWiiloadHeader(makeHeader(0, 5, kWiiloadMaxArgsBytes + 1, 100, 0)).isOk());
}

TEST_CASE("decodeWiiloadPayload handles raw and zlib payloads") {
    std::vector<u8> plain = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'i', 'i', 'l', 'o', 'a', 'd', '!'};
    WiiloadHeader rawHdr{0, 5, 0, static_cast<u32>(plain.size()), 0};
    auto rawRes = decodeWiiloadPayload(rawHdr, plain);
    REQUIRE(rawRes.isOk());
    CHECK(rawRes.value() == plain);

    auto z = makeZlib(plain);
    WiiloadHeader zHdr{0, 5, 0, static_cast<u32>(z.size()), static_cast<u32>(plain.size())};
    auto zRes = decodeWiiloadPayload(zHdr, z);
    REQUIRE(zRes.isOk());
    CHECK(zRes.value() == plain);

    // Corrupt adler32 trailer
    auto badZ = z;
    badZ.back() ^= 0xFF;
    CHECK_FALSE(decodeWiiloadPayload(zHdr, badZ).isOk());

    // Mismatched uncompressed size
    WiiloadHeader wrongLen{0, 5, 0, static_cast<u32>(z.size()), static_cast<u32>(plain.size() + 1)};
    CHECK_FALSE(decodeWiiloadPayload(wrongLen, z).isOk());
}

TEST_CASE("parseWiiloadArgs splits NUL-separated arguments and detectWiiloadKind inspects magic") {
    const u8 raw[] = {'a', 'p', 'p', '.', 'd', 'o', 'l', 0, '-', '-', 'f', 'o', 'o', 0};
    auto args = parseWiiloadArgs(raw);
    REQUIRE(args.size() == 2);
    CHECK(args[0] == "app.dol");
    CHECK(args[1] == "--foo");
    CHECK(parseWiiloadArgs({}).empty());

    const u8 zipMagic[] = {'P', 'K', 0x03, 0x04, 0};
    const u8 elfMagic[] = {0x7F, 'E', 'L', 'F', 0};
    const u8 dolBytes[] = {0x00, 0x00, 0x01, 0x00, 0};
    CHECK(detectWiiloadKind(zipMagic) == WiiloadKind::Zip);
    CHECK(detectWiiloadKind(elfMagic) == WiiloadKind::Elf);
    CHECK(detectWiiloadKind(dolBytes) == WiiloadKind::Dol);
}
