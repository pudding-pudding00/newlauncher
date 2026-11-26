#include "core/hmac.hpp"
#include "core/sha256.hpp"

#include <doctest/doctest.h>

#include <string_view>
#include <vector>

using namespace nl;

namespace {
std::span<const u8> bytes(std::string_view s) { return {reinterpret_cast<const u8*>(s.data()), s.size()}; }
}  // namespace

TEST_CASE("hmacSha256: RFC 4231 Fall 1") {
    const std::vector<u8> key(20, 0x0b);
    CHECK(bytesToHex(hmacSha256(key, bytes("Hi There"))) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST_CASE("hmacSha256: RFC 4231 Fall 2") {
    CHECK(bytesToHex(hmacSha256(bytes("Jefe"), bytes("what do ya want for nothing?"))) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("hmacSha256: RFC 4231 Fall 6, Schlüssel länger als ein Block") {
    const std::vector<u8> key(131, 0xaa);
    CHECK(bytesToHex(hmacSha256(key, bytes("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE("constantTimeEqual") {
    const u8 a[] = {1, 2, 3};
    const u8 b[] = {1, 2, 3};
    const u8 c[] = {1, 2, 4};
    const u8 d[] = {1, 2};
    CHECK(constantTimeEqual(a, b));
    CHECK_FALSE(constantTimeEqual(a, c));
    CHECK_FALSE(constantTimeEqual(a, d));
}
