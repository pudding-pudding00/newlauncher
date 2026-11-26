#include "core/token_store.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace nl;

namespace {
const std::vector<u8> kDev = {0x12, 0x34, 0x56, 0x78, 0xde, 0xad};
constexpr std::array<u8, 16> kSalt = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
constexpr std::array<u8, 16> kIv = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};

StoredTokens sample() {
    StoredTokens t;
    t.provider = "drive";
    t.account = "0123456789";
    t.tokens = TokenSet{"ya29.geheimer-zugang", "1//geheimes-refresh", 12345};
    return t;
}

bool contains(const std::vector<u8>& blob, std::string_view needle) {
    return std::search(blob.begin(), blob.end(), needle.begin(), needle.end()) != blob.end();
}
}  // namespace

TEST_CASE("token_store: Rundlauf") {
    const auto blob = sealTokens(sample(), kDev, kSalt, kIv);
    REQUIRE(blob.size() > 4 + 1 + 16 + 16 + 4 + 32);
    CHECK(std::string(blob.begin(), blob.begin() + 4) == "NLTK");
    const auto back = unsealTokens(blob, kDev);
    REQUIRE(back.has_value());
    CHECK(back->provider == "drive");
    CHECK(back->account == "0123456789");
    CHECK(back->tokens.access == "ya29.geheimer-zugang");
    CHECK(back->tokens.refresh == "1//geheimes-refresh");
    CHECK(back->tokens.expiresAtMs == 0);
}

TEST_CASE("token_store: kein Klartext im Blob") {
    const auto blob = sealTokens(sample(), kDev, kSalt, kIv);
    CHECK_FALSE(contains(blob, "ya29"));
    CHECK_FALSE(contains(blob, "geheim"));
    CHECK_FALSE(contains(blob, "drive"));
}

TEST_CASE("token_store: jede Manipulation wird erkannt") {
    const auto blob = sealTokens(sample(), kDev, kSalt, kIv);
    // salt, iv, Länge, Chiffrat und MAC: je ein gekipptes Bit
    for (const std::size_t pos : {std::size_t{5}, std::size_t{21}, std::size_t{37}, std::size_t{41},
                                  blob.size() - 40, blob.size() - 1}) {
        auto bad = blob;
        bad[pos] ^= 0x01;
        CHECK_FALSE(unsealTokens(bad, kDev).has_value());
    }
    auto shortBlob = blob;
    shortBlob.resize(blob.size() - 1);
    CHECK_FALSE(unsealTokens(shortBlob, kDev).has_value());
    CHECK_FALSE(unsealTokens({}, kDev).has_value());
}

TEST_CASE("token_store: falsche Geräte-ID") {
    const auto blob = sealTokens(sample(), kDev, kSalt, kIv);
    const std::vector<u8> other = {0x12, 0x34, 0x56, 0x78, 0xde, 0xae};
    CHECK_FALSE(unsealTokens(blob, other).has_value());
}

TEST_CASE("redactSecret") {
    CHECK(redactSecret("ya29.abc") == "<geheim:8>");
    CHECK(redactSecret("") == "<geheim:0>");
}
