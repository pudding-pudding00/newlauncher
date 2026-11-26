#include "core/deflate.hpp"
#include "core/inflate.hpp"
#include <doctest/doctest.h>
#include <string>
#include <vector>
using namespace nl;

namespace {
std::vector<u8> roundTrip(const std::vector<u8>& in) {
    const std::vector<u8> packed = deflateRaw(in);
    auto r = inflateRaw(packed, in.size());
    CHECK(r.isOk());  // REQUIRE bricht ohne Exceptions das ganze Programm ab
    return r.isOk() ? r.value() : std::vector<u8>{0xEE};
}

std::vector<u8> pseudoRandom(std::size_t n, u32 seed) {
    std::vector<u8> v(n);
    for (auto& b : v) {
        seed = seed * 1664525u + 1013904223u;
        b = static_cast<u8>(seed >> 24);
    }
    return v;
}
}  // namespace

TEST_CASE("deflateRaw: Rundlauf leer") {
    const std::vector<u8> in;
    CHECK(roundTrip(in) == in);
}

TEST_CASE("deflateRaw: Rundlauf 1 Byte") {
    const std::vector<u8> in{0x42};
    CHECK(roundTrip(in) == in);
}

TEST_CASE("deflateRaw: 1 MiB Nullen werden klein") {
    const std::vector<u8> in(1024 * 1024, 0);
    CHECK(deflateRaw(in).size() < 8192);  // feste Codes: 4064 Treffer × 13 Bit ≈ 6,6 KB
    CHECK(roundTrip(in) == in);
}

TEST_CASE("deflateRaw: Zufall wächst höchstens um gespeicherte Blockköpfe") {
    const std::vector<u8> in = pseudoRandom(200 * 1024, 7);
    const std::size_t blocks = (in.size() + 65534) / 65535;
    CHECK(deflateRaw(in).size() <= in.size() + 5 * blocks);
    CHECK(roundTrip(in) == in);
}

TEST_CASE("deflateRaw: Wiederholungen über 32 KiB Abstand") {
    std::vector<u8> in = pseudoRandom(40 * 1024, 3);
    const std::vector<u8> copy(in.begin(), in.begin() + 1000);
    in.insert(in.end(), copy.begin(), copy.end());  // Abstand > 32 KiB: kein Rückverweis erlaubt
    const std::string text = "Speicherstand Speicherstand Speicherstand ";
    for (int i = 0; i < 500; ++i) in.insert(in.end(), text.begin(), text.end());
    CHECK(roundTrip(in) == in);
}

TEST_CASE("deflateRaw: Text wird kleiner") {
    std::string s;
    for (int i = 0; i < 200; ++i) s += "Link speichert im Tempel des Waldes. ";
    const std::vector<u8> in(s.begin(), s.end());
    CHECK(deflateRaw(in).size() < in.size() / 4);
    CHECK(roundTrip(in) == in);
}
