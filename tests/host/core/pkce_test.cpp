#include "core/pkce.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cctype>

using namespace nl;

TEST_CASE("makePkce: RFC 7636 Anhang B") {
    const std::array<u8, 32> random = {116, 24,  223, 180, 151, 153, 224, 37,  79, 250, 96,  125, 216, 173, 187, 186,
                                       22,  212, 37,  77,  105, 214, 191, 240, 91, 88,  5,   88,  83,  132, 141, 121};
    const Pkce p = makePkce(random);
    CHECK(p.verifier == "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
    CHECK(p.challenge == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST_CASE("makePkce: Verifier hat 43 Zeichen aus dem URL-Alphabet") {
    std::array<u8, 32> random{};
    random.fill(0xff);
    const Pkce p = makePkce(random);
    CHECK(p.verifier.size() == 43);
    CHECK(p.challenge.size() == 43);
    for (char c : p.verifier) CHECK((std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'));
}
