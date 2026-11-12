#include "core/device_names.hpp"

namespace nl {
namespace {

// M16 hängt NTFS, ext und WBFS ein; bis dahin nennt der Grund, was fehlt.
const char* unsupportedReason(FsKind kind) {
    switch (kind) {
        case FsKind::ExFat: return "exFAT wird nicht unterstützt – bitte FAT32";
        case FsKind::Ntfs: return "NTFS noch nicht unterstützt";
        case FsKind::Ext: return "ext noch nicht unterstützt";
        case FsKind::Wbfs: return "WBFS-Partition noch nicht unterstützt";
        case FsKind::Fat: return "";
        case FsKind::Unknown: break;
    }
    return "Dateisystem unbekannt";
}

}  // namespace

std::vector<DeviceSlot> assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts) {
    std::vector<DeviceSlot> out;
    if (sdPresent) {
        const bool fat = sdKind == FsKind::Fat;
        out.push_back(DeviceSlot{"sd", false, 0, sdKind, fat, fat ? "" : unsupportedReason(sdKind)});
    }
    u32 fatCount = 0;
    for (const PartitionInfo& p : usbParts) {
        DeviceSlot s{"", true, p.lba, p.kind, false, ""};
        if (p.beyondLimit) {
            s.reason = "hinter der 32-Bit-Sektorgrenze (2 TiB)";  // libfat und BlockDevice adressieren mit u32
        } else if (p.kind != FsKind::Fat) {
            s.reason = unsupportedReason(p.kind);
        } else if (fatCount >= kMaxUsbMounts) {
            s.reason = "zu viele Partitionen";
        } else {
            ++fatCount;
            s.name = fatCount == 1 ? "usb" : "usb" + std::to_string(fatCount);
            s.mount = true;
        }
        out.push_back(std::move(s));
    }
    return out;
}

const char* fsKindName(FsKind kind) {
    switch (kind) {
        case FsKind::Fat: return "fat";
        case FsKind::ExFat: return "exfat";
        case FsKind::Ntfs: return "ntfs";
        case FsKind::Ext: return "ext";
        case FsKind::Wbfs: return "wbfs";
        case FsKind::Unknown: break;
    }
    return "unknown";
}

}  // namespace nl
