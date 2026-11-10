#include "core/n64_choice.hpp"
#include <doctest/doctest.h>
#include <vector>
using namespace nl;

namespace {
constexpr u64 kId = 0x000100014E414D50ULL;
const std::vector<u64> kInstalled{0x0001000148414141ULL, kId};
std::size_t at(N64Method m) { return static_cast<std::size_t>(m); }
}  // namespace

TEST_CASE("ohne alles: kein Weg, Gründe NotMapped/DolMissing/DolMissing") {
    const N64Decision d = decideN64({});
    CHECK_FALSE(d.chosen.has_value());
    CHECK(d.why[at(N64Method::Vc)] == N64Unavailable::NotMapped);
    CHECK(d.why[at(N64Method::Not64)] == N64Unavailable::DolMissing);
    CHECK(d.why[at(N64Method::Wii64)] == N64Unavailable::DolMissing);
    CHECK_FALSE(d.overrideIgnored);
    CHECK_FALSE(d.mappingIgnored);
}

TEST_CASE("gemapptes und installiertes Inject schlägt Not64") {
    N64Inputs in;
    in.mappedTitle = kId;
    in.installedTitles = kInstalled;
    in.not64Dol = true;
    const N64Decision d = decideN64(in);
    CHECK(d.chosen == N64Method::Vc);
    CHECK(d.available(N64Method::Vc));
    CHECK(d.available(N64Method::Not64));
    CHECK_FALSE(d.available(N64Method::Wii64));
}

TEST_CASE("gemappt, aber nicht installiert → Not64, mappingIgnored") {
    N64Inputs in;
    in.mappedTitle = 0x000100014E4B5450ULL;
    in.installedTitles = kInstalled;
    in.not64Dol = true;
    const N64Decision d = decideN64(in);
    CHECK(d.chosen == N64Method::Not64);
    CHECK(d.why[at(N64Method::Vc)] == N64Unavailable::NotInstalled);
    CHECK(d.mappingIgnored);
}

TEST_CASE("nur Wii64-DOL → Wii64") {
    N64Inputs in;
    in.wii64Dol = true;
    CHECK(decideN64(in).chosen == N64Method::Wii64);
}

TEST_CASE("verfügbarer Override gewinnt") {
    N64Inputs in;
    in.override_ = N64Method::Wii64;
    in.mappedTitle = kId;
    in.installedTitles = kInstalled;
    in.not64Dol = in.wii64Dol = true;
    const N64Decision d = decideN64(in);
    CHECK(d.chosen == N64Method::Wii64);
    CHECK_FALSE(d.overrideIgnored);
}

TEST_CASE("nicht verfügbarer Override → weiter mit Schritt 2, overrideIgnored") {
    N64Inputs in;
    in.override_ = N64Method::Wii64;
    in.mappedTitle = kId;
    in.installedTitles = kInstalled;
    in.not64Dol = true;
    const N64Decision d = decideN64(in);
    CHECK(d.chosen == N64Method::Vc);
    CHECK(d.overrideIgnored);
}

TEST_CASE("Override VC ohne Zuordnung → Not64, overrideIgnored") {
    N64Inputs in;
    in.override_ = N64Method::Vc;
    in.not64Dol = true;
    const N64Decision d = decideN64(in);
    CHECK(d.chosen == N64Method::Not64);
    CHECK(d.overrideIgnored);
    CHECK_FALSE(d.mappingIgnored);
}
