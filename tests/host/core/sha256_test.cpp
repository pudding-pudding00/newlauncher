#include "core/sha256.hpp"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace nl;

namespace {
std::string hashHex(std::string_view s) {
    const auto h = sha256(std::span<const u8>(reinterpret_cast<const u8*>(s.data()), s.size()));
    return bytesToHex(h);
}
}  // namespace

TEST_CASE("sha256: FIPS-180-4-Vektoren") {
    CHECK(hashHex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hashHex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hashHex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(hashHex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: Längen an den Blockgrenzen 55/56/63/64 Byte") {
    // Referenzwerte aus Python hashlib
    CHECK(hashHex(std::string(55, 'x')) == "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
    CHECK(hashHex(std::string(56, 'x')) == "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
    CHECK(hashHex(std::string(64, 'x')) == "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
}

TEST_CASE("bytesToHex/hexToBytes") {
    const u8 b[3] = {0x00, 0xAB, 0x7F};
    CHECK(bytesToHex(b) == "00ab7f");
    std::array<u8, 3> out{};
    CHECK(hexToBytes("00AB7f", out));
    CHECK(out == std::array<u8, 3>{0x00, 0xAB, 0x7F});
    CHECK_FALSE(hexToBytes("00ab7", out));   // falsche Länge
    CHECK_FALSE(hexToBytes("00ab7g", out));  // keine Hexziffer
}
