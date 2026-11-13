#include "core/device_names.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace nl;

namespace {
PartitionInfo part(u64 lba, FsKind kind) { return PartitionInfo{lba, 1000, 0x0C, kind, false, false}; }
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
    CHECK(s[2].skip == SkipReason::ExFat);
    CHECK(std::string(skipReasonText(s[2].skip, true)) == "exFAT wird nicht unterstützt – bitte FAT32");  // Plan M12
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
    CHECK(s[8].skip == SkipReason::TooMany);
    CHECK(s[9].skip == SkipReason::TooMany);
    CHECK(std::string(skipReasonText(SkipReason::TooMany, true)) == "zu viele Partitionen");
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
    CHECK(n[0].skip != SkipReason::None);
}

TEST_CASE("device_names: fsKindName für das Log") {
    CHECK(std::string(fsKindName(FsKind::Fat)) == "fat");
    CHECK(std::string(fsKindName(FsKind::ExFat)) == "exfat");
    CHECK(std::string(fsKindName(FsKind::Wbfs)) == "wbfs");
    CHECK(std::string(fsKindName(FsKind::Unknown)) == "unknown");
}

TEST_CASE("device_names: FAT-Partition hinter der Sektorgrenze bekommt keinen Namen, aber einen Grund") {
    PartitionInfo far = part(9000, FsKind::Fat);
    far.beyondLimit = true;
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat), far, part(20000, FsKind::Fat)};
    const auto s = assignDevices(false, FsKind::Unknown, usb);
    REQUIRE(s.size() == 3);
    CHECK(s[0].name == "usb");
    CHECK(s[1].name.empty());
    CHECK_FALSE(s[1].mount);
    CHECK(s[1].skip == SkipReason::BeyondLimit);
    CHECK(std::string(skipReasonText(s[1].skip, true)) == "hinter der 32-Bit-Sektorgrenze (2 TiB)");
    CHECK(s[2].name == "usb2");
}

// M16 Task 2: NTFS (und ext, falls je unterstützt) bekommen den nächsten freien Namen, nur lesend.
TEST_CASE("device_names: FAT + NTFS mit ntfs=true → usb (fat), usb2 (ntfs, nur lesend)") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat), part(9000, FsKind::Ntfs)};
    const auto s = assignDevices(false, FsKind::Unknown, usb, FsSupport{true, false, false});
    REQUIRE(s.size() == 2);
    CHECK(s[0].name == "usb");
    CHECK(s[0].mount);
    CHECK_FALSE(s[0].readOnly);
    CHECK(s[1].name == "usb2");
    CHECK(s[1].kind == FsKind::Ntfs);
    CHECK(s[1].mount);
    CHECK(s[1].readOnly);
    CHECK(s[1].skip == SkipReason::None);
}

TEST_CASE("device_names: NTFS ohne Unterstützung bleibt ohne Namen, Grund DE/EN") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Fat), part(9000, FsKind::Ntfs)};
    const auto s = assignDevices(false, FsKind::Unknown, usb, FsSupport{false, false, false});
    REQUIRE(s.size() == 2);
    CHECK(s[1].name.empty());
    CHECK_FALSE(s[1].mount);
    CHECK(s[1].skip == SkipReason::NtfsUnsupported);
    CHECK(std::string(skipReasonText(s[1].skip, true)) == "NTFS wird nicht unterstützt");
    CHECK(std::string(skipReasonText(s[1].skip, false)) == "NTFS is not supported");
    // alter Aufruf ohne FsSupport: nichts außer FAT
    const auto old = assignDevices(false, FsKind::Unknown, usb);
    CHECK_FALSE(old[1].mount);
}

TEST_CASE("device_names: ext4 vor FAT → usb (ext), usb2 (fat); ohne ext-Unterstützung usb (fat)") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::Ext), part(9000, FsKind::Fat)};
    const auto on = assignDevices(false, FsKind::Unknown, usb, FsSupport{false, true, false});
    REQUIRE(on.size() == 2);
    CHECK(on[0].name == "usb");
    CHECK(on[0].readOnly);
    CHECK(on[1].name == "usb2");
    const auto off = assignDevices(false, FsKind::Unknown, usb, FsSupport{true, false, false});
    CHECK(off[0].name.empty());
    CHECK(off[0].skip == SkipReason::ExtUnsupported);
    CHECK(std::string(skipReasonText(off[0].skip, true)) == "ext wird nicht unterstützt");
    CHECK(std::string(skipReasonText(off[0].skip, false)) == "ext is not supported");
    CHECK(off[1].name == "usb");
}

TEST_CASE("device_names: exFAT bleibt auch mit voller Unterstützung ohne Namen") {
    const std::vector<PartitionInfo> usb{part(2048, FsKind::ExFat)};
    const auto s = assignDevices(false, FsKind::Unknown, usb, FsSupport{true, true, true});
    CHECK_FALSE(s[0].mount);
    CHECK(s[0].skip == SkipReason::ExFat);
    CHECK(std::string(skipReasonText(s[0].skip, false)) == "exFAT is not supported – please use FAT32");
}
