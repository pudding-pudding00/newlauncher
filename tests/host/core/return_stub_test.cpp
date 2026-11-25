// M20 (Spec 4.2): Parameterblock des Rückkehr-Stubs.
#include "core/return_stub.hpp"

#include "core/launch_args.hpp"
#include "launch/return_stub/stub_check.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace nl;

namespace {

u32 be(const std::vector<u8>& b, std::size_t at) { return nlrs_be32(b.data() + at); }

}  // namespace

TEST_CASE("buildReturnParams: Layout, Prüfsumme, vom Stub akzeptiert") {
    const std::vector<Extent> ext{{0, 1000, 3}, {3, 5000, 2}};
    const std::vector<u8> argv{1, 2, 3, 4, 5};
    const auto r = buildReturnParams(2100, ext, argv);
    REQUIRE(r.isOk());
    const auto& b = r.value();
    CHECK(b.size() == 0x18 + 8 + 16);
    CHECK(be(b, 0x00) == NLRS_MAGIC);
    CHECK(be(b, 0x04) == NLRS_VERSION);
    CHECK(be(b, 0x08) == 2100);
    CHECK(be(b, 0x0C) == 2);
    CHECK(be(b, 0x10) == 5);
    CHECK(b[0x18] == 1);
    CHECK(b[0x1C] == 5);
    CHECK(b[0x1D] == 0);  // aufgefüllt
    CHECK(be(b, 0x20) == 1000);
    CHECK(be(b, 0x24) == 3);
    CHECK(be(b, 0x28) == 5000);
    CHECK(be(b, 0x2C) == 2);
    CHECK(nlrs_params_ok(b.data()) == 1);
}

TEST_CASE("buildReturnParams: ohne argv") {
    const std::vector<Extent> ext{{0, 77, 1}};
    const auto r = buildReturnParams(512, ext, {});
    REQUIRE(r.isOk());
    CHECK(r.value().size() == 0x18 + 8);
    CHECK(nlrs_params_ok(r.value().data()) == 1);
}

TEST_CASE("buildReturnParams: Fehler") {
    const std::vector<Extent> one{{0, 77, 1}};
    CHECK(buildReturnParams(512, {}, {}).error() == ReturnStubError::NoExtents);
    CHECK(buildReturnParams(513, one, {}).error() == ReturnStubError::SectorMismatch);
    CHECK(buildReturnParams(0, one, {}).error() == ReturnStubError::DolSize);
    CHECK(buildReturnParams(NLRS_MAX_DOL + 1, one, {}).error() == ReturnStubError::DolSize);
    const std::vector<u8> big(NLRS_ARGV_MAX + 1, 'x');
    CHECK(buildReturnParams(512, one, big).error() == ReturnStubError::ArgvTooLarge);
    const std::vector<Extent> gap{{0, 10, 1}, {2, 20, 1}};  // Dateisektor 1 fehlt
    CHECK(buildReturnParams(1024, gap, {}).error() == ReturnStubError::NotContiguous);
    std::vector<Extent> many;
    for (u32 i = 0; i < 300; ++i) many.push_back({i, 10 + 2 * i, 1});
    CHECK(buildReturnParams(300 * 512, many, {}).error() == ReturnStubError::TooManyExtents);
}

TEST_CASE("buildReturnParams: argv-Block von packArgvBlock passt an seine Zieladresse") {
    const std::vector<std::string> args{"sd:/apps/newlauncher/boot.dol"};
    const auto argv = packArgvBlock(args, kReturnArgvAddr);
    REQUIRE(argv.isOk());
    CHECK(kReturnArgvAddr == NLRS_PARAMS_ADDR + NLRS_HEAD_BYTES);
    const std::vector<Extent> ext{{0, 77, 1}};
    const auto r = buildReturnParams(512, ext, argv.value());
    REQUIRE(r.isOk());
    CHECK(nlrs_params_ok(r.value().data()) == 1);
}
