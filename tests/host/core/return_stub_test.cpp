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
    CHECK(b.size() == 0x18 + 8 + NLRS_ARGV_SLACK + 16);
    CHECK(be(b, 0x00) == NLRS_MAGIC);
    CHECK(be(b, 0x04) == NLRS_VERSION);
    CHECK(be(b, 0x08) == 2100);
    CHECK(be(b, 0x0C) == 2);
    CHECK(be(b, 0x10) == 5);
    CHECK(b[0x18] == 1);
    CHECK(b[0x1C] == 5);
    CHECK(b[0x1D] == 0);  // aufgefüllt
    CHECK(be(b, 0x60) == 1000);
    CHECK(be(b, 0x64) == 3);
    CHECK(be(b, 0x68) == 5000);
    CHECK(be(b, 0x6C) == 2);
    CHECK(nlrs_params_ok(b.data()) == 1);
}

TEST_CASE("buildReturnParams: Cluster-Extents werden auf die Dateigröße gekürzt") {
    // fileExtents liefert ganze Cluster (hier 8 Sektoren); die Datei braucht nur 2100 Byte = 5 Sektoren.
    const std::vector<Extent> ext{{0, 1000, 4}, {4, 5000, 4}};
    const auto r = buildReturnParams(2100, ext, {});
    REQUIRE(r.isOk());
    const auto& b = r.value();
    CHECK(be(b, 0x0C) == 2);
    CHECK(be(b, 0x5C) == 4);
    CHECK(be(b, 0x64) == 1);
    CHECK(nlrs_params_ok(b.data()) == 1);
    // Ganz überzählige Extents am Ende fallen weg.
    const std::vector<Extent> extra{{0, 1000, 8}, {8, 5000, 8}};
    const auto r2 = buildReturnParams(2100, extra, {});
    REQUIRE(r2.isOk());
    CHECK(be(r2.value(), 0x0C) == 1);
    CHECK(be(r2.value(), 0x5C) == 5);
    CHECK(nlrs_params_ok(r2.value().data()) == 1);
}

TEST_CASE("buildReturnParams: ohne argv") {
    const std::vector<Extent> ext{{0, 77, 1}};
    const auto r = buildReturnParams(512, ext, {});
    REQUIRE(r.isOk());
    CHECK(r.value().size() == 0x18 + NLRS_ARGV_SLACK + 8);
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

TEST_CASE("buildReturnParams: libogc-Zeigerfeld hinter der Kommandozeile beschädigt den Block nicht") {
    // libogc build_argv legt argv[] bei (commandLine + length + 4) & ~3 ab, mitten in den Parameterblock.
    const std::vector<std::string> args{"sd:/apps/newlauncher/boot.dol"};
    const auto argv = packArgvBlock(args, kReturnArgvAddr);
    REQUIRE(argv.isOk());
    const std::vector<Extent> ext{{0, 77, 1}};
    const auto r = buildReturnParams(512, ext, argv.value());
    REQUIRE(r.isOk());
    std::vector<u8> b = r.value();
    const u32 cmdLine = nlrs_be32(b.data() + 0x18 + 4) - NLRS_PARAMS_ADDR;  // __argv.commandLine
    const u32 len = nlrs_be32(b.data() + 0x18 + 8);                           // __argv.length
    const u32 at = (cmdLine + len + 4) & ~3u;
    const u32 words = static_cast<u32>(args.size()) + 2;  // Zeiger, NULL, Reserve
    REQUIRE(at + words * 4 <= b.size());
    for (u32 i = 0; i < words * 4; ++i) b[at + i] = 0xA5;
    CHECK(nlrs_params_ok(b.data()) == 1);
}

TEST_CASE("buildReturnParams: zu viele Argumente für den Freiraum") {
    std::vector<std::string> args(NLRS_ARGV_SLACK / 4, "x");
    const auto argv = packArgvBlock(args, kReturnArgvAddr);
    REQUIRE(argv.isOk());
    const std::vector<Extent> ext{{0, 77, 1}};
    CHECK(buildReturnParams(512, ext, argv.value()).error() == ReturnStubError::ArgvTooLarge);
}

#include "core/launch_layout.hpp"

TEST_CASE("Stub-Konstanten passen zum Speicherlayout des Chainloaders") {
    CHECK(NLRS_BUFFER_ADDR == kDolBufferAddr);
    CHECK(NLRS_MAX_DOL == kMaxDolBytes);
    CHECK(NLRS_MEM1_BEGIN == kMem1Begin);
    CHECK(NLRS_MEM1_END == kMem1End);
    CHECK(NLRS_MEM2_BEGIN == kMem2Begin);
    CHECK(NLRS_STUB_ADDR + NLRS_STUB_MAX == NLRS_PARAMS_ADDR);
    CHECK(NLRS_PARAMS_ADDR + NLRS_PARAMS_MAX <= NLRS_STAGE_ADDR);
}
