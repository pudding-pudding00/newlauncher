#include <doctest/doctest.h>

#include <vector>

#include "core/ftp_vfs.hpp"

using namespace nl;

TEST_CASE("resolveFtpPath handles virtual root, mounts, normalization, and jail rules") {
    const std::vector<FtpMount> mounts = {{"sd", true}, {"usb", true}, {"usb2", false}};

    auto root = resolveFtpPath("/", "", mounts);
    REQUIRE(root.isOk());
    CHECK(root.value().isRoot);
    CHECK(root.value().virtualPath == "/");
    CHECK_FALSE(ftpMutationAllowed(root.value()));

    auto sdRoot = resolveFtpPath("/", "/sd/", mounts);
    REQUIRE(sdRoot.isOk());
    CHECK(sdRoot.value().isMountRoot);
    CHECK(sdRoot.value().fsPath == "sd:/");
    CHECK(sdRoot.value().writable);
    CHECK_FALSE(ftpMutationAllowed(sdRoot.value()));

    auto file = resolveFtpPath("/sd/apps", "../games/./test.iso", mounts);
    REQUIRE(file.isOk());
    CHECK(file.value().virtualPath == "/sd/games/test.iso");
    CHECK(file.value().fsPath == "sd:/games/test.iso");
    CHECK(ftpMutationAllowed(file.value()));

    // Clamp .. at /
    auto clamped = resolveFtpPath("/sd", "../../../..", mounts);
    REQUIRE(clamped.isOk());
    CHECK(clamped.value().isRoot);

    // Read-only mount
    auto ro = resolveFtpPath("/", "/usb2/wbfs/game.wbfs", mounts);
    REQUIRE(ro.isOk());
    CHECK_FALSE(ro.value().writable);
    CHECK_FALSE(ftpMutationAllowed(ro.value()));

    // Forbidden paths: nand, colons, backslashes, control chars
    CHECK_FALSE(resolveFtpPath("/", "/nand", mounts).isOk());
    CHECK_FALSE(resolveFtpPath("/", "/nand/title", mounts).isOk());
    CHECK_FALSE(resolveFtpPath("/", "/sd/foo:bar", mounts).isOk());
    CHECK_FALSE(resolveFtpPath("/", "/sd\\evil", mounts).isOk());
    CHECK_FALSE(resolveFtpPath("/", "/sd/a\nb", mounts).isOk());

    // Rename rules
    auto a = resolveFtpPath("/", "/sd/a", mounts).value();
    auto b = resolveFtpPath("/", "/sd/b", mounts).value();
    auto u = resolveFtpPath("/", "/usb/b", mounts).value();
    CHECK(ftpRenameAllowed(a, b));
    CHECK_FALSE(ftpRenameAllowed(a, u));
}
