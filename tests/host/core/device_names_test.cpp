#include "core/device_names.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace nl;

namespace {
PartitionInfo part(u64 lba, FsKind kind) { return PartitionInfo{lba, 1000, 0x0C, kind, false}; }
}  // namespace

TEST_CASE("device_names: SD und zwei FAT-Partitionen → sd, usb, usb2") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat), part(9000, FsKind::Fat)};
    const auto s = assignDevices(true, FsKind::Fat, usb);
    REQUIRE(s.size() == 3);
    CHECK(s[0].name == "sd");
    CHECK_FALSE(s[0].usb);
    CHECK(s[0].mount);
    CHECK(s[1].name == "usb");
    CHECK(s[1].usb);
    CHECK(s[1].lba == 2048);
    CHECK(s[1].mount);
    CHECK(s[2].name == "usb2");
    CHECK(s[2].lba == 9000);
    CHECK(s[2].mount);
}

TEST_CASE("device_names: exFAT zwischen zwei FAT-Partitionen bekommt keinen Namen") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat), part(5000, FsKind::ExFat), part(9000, FsKind::Fat)};
    const auto s = assignDevices(true, FsKind::Fat, usb);
    REQUIRE(s.size() == 4);
    CHECK(s[1].name == "usb");
    CHECK(s[2].name.empty());
    CHECK_FALSE(s[2].mount);
    CHECK(s[2].kind == FsKind::ExFat);
    CHECK(s[2].reason == "exFAT wird nicht unterstützt – bitte FAT32");  // Plan M12, Spec v2: klarer Hinweis
    CHECK(s[3].name == "usb2");
    CHECK(s[3].mount);
}

TEST_CASE("device_names: zehn FAT-Partitionen → acht eingehängt, zwei mit Grund") {
    std::vector<PartitionInfo> usb;
    for (u64 i = 0; i < 10; ++i) usb.push_back(part(2048 + i * 1000, FsKind::Fat));
    const auto s = assignDevices(false, FsKind::Unknown, usb);
    REQUIRE(s.size() == 10);
    int mounted = 0;
    for (const auto& d : s) mounted += d.mount ? 1 : 0;
    CHECK(mounted == 8);
    CHECK(s[7].name == "usb8");
    CHECK_FALSE(s[8].mount);
    CHECK(s[8].reason == "zu viele Partitionen");
    CHECK(s[9].reason == "zu viele Partitionen");
    CHECK(s[9].name.empty());
}

TEST_CASE("device_names: ohne SD kein sd-Eintrag, SD ohne FAT wird nicht eingehängt") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat)};
    const auto s = assignDevices(false, FsKind::Unknown, usb);
    REQUIRE(s.size() == 1);
    CHECK(s[0].name == "usb");

    const auto n = assignDevices(true, FsKind::Ntfs, {});
    REQUIRE(n.size() == 1);
    CHECK(n[0].name == "sd");
    CHECK_FALSE(n[0].mount);
    CHECK_FALSE(n[0].reason.empty());
}

TEST_CASE("device_names: fsKindName für das Log") {
    CHECK(std::string(fsKindName(FsKind::Fat)) == "fat");
    CHECK(std::string(fsKindName(FsKind::ExFat)) == "exfat");
    CHECK(std::string(fsKindName(FsKind::Wbfs)) == "wbfs");
    CHECK(std::string(fsKindName(FsKind::Unknown)) == "unknown");
}
