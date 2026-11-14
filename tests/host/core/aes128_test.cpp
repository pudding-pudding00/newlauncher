#include "core/aes128.hpp"

#include <doctest/doctest.h>

#include <string_view>
#include <vector>

using namespace nl;

namespace {
std::vector<u8> hex(std::string_view s) {
    std::vector<u8> out;
    auto nib = [](char c) { return static_cast<u8>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<u8>(nib(s[i]) << 4 | nib(s[i + 1])));
    return out;
}
AesBlock block(std::string_view s) {
    AesBlock b{};
    const auto v = hex(s);
    for (std::size_t i = 0; i < 16; ++i) b[i] = v[i];
    return b;
}
}  // namespace

TEST_CASE("aes128: FIPS-197 Anhang C.1 und Anhang B") {
    struct V {
        const char *key, *plain, *cipher;
    };
    for (const V& v : {V{"000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff",
                         "69c4e0d86a7b0430d8cdb78070b4c55a"},
                       V{"2b7e151628aed2a6abf7158809cf4f3c", "3243f6a8885a308d313198a2e0370734",
                         "3925841d02dc09fbdc118597196a0b32"}}) {
        const Aes128 aes(block(v.key));
        AesBlock out{};
        const AesBlock plain = block(v.plain);
        aes.encryptBlock(plain.data(), out.data());
        CHECK(out == block(v.cipher));
        aes.decryptBlock(out.data(), out.data());  // an Ort und Stelle
        CHECK(out == plain);
    }
}

TEST_CASE("aes128: CBC nach SP 800-38A F.2.1/F.2.2, an Ort und Stelle") {
    const Aes128 aes(block("2b7e151628aed2a6abf7158809cf4f3c"));
    const AesBlock iv = block("000102030405060708090a0b0c0d0e0f");
    const auto plain = hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    const auto cipher = hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    std::vector<u8> buf = plain;
    REQUIRE(aesCbcEncrypt(aes, iv, buf, buf));
    CHECK(buf == cipher);
    REQUIRE(aesCbcDecrypt(aes, iv, buf, buf));
    CHECK(buf == plain);
    std::vector<u8> out(cipher.size());
    REQUIRE(aesCbcDecrypt(aes, iv, cipher, out));
    CHECK(out == plain);
}

TEST_CASE("aes128: CBC lehnt Teilblöcke und ungleiche Längen ab") {
    const Aes128 aes(AesBlock{});
    std::vector<u8> a(17), b(17), c(16);
    CHECK_FALSE(aesCbcDecrypt(aes, AesBlock{}, a, b));
    CHECK_FALSE(aesCbcDecrypt(aes, AesBlock{}, c, a));
    CHECK(aesCbcDecrypt(aes, AesBlock{}, std::span<const u8>(), std::span<u8>()));
}
