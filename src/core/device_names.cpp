#include "core/device_names.hpp"

namespace nl {
namespace {

SkipReason unsupported(FsKind kind) {
    switch (kind) {
        case FsKind::ExFat: return SkipReason::ExFat;
        case FsKind::Ntfs: return SkipReason::NtfsUnsupported;
        case FsKind::Ext: return SkipReason::ExtUnsupported;
        case FsKind::Wbfs: return SkipReason::WbfsUnsupported;
        case FsKind::Fat: return SkipReason::None;
        case FsKind::Unknown: break;
    }
    return SkipReason::UnknownFs;
}

bool mountable(FsKind kind, FsSupport support) {
    return kind == FsKind::Fat || (kind == FsKind::Ntfs && support.ntfs) || (kind == FsKind::Ext && support.ext) ||
           (kind == FsKind::Wbfs && support.wbfs);
}

}  // namespace

std::vector<DeviceSlot> assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts,
                                      FsSupport support) {
    std::vector<DeviceSlot> out;
    if (sdPresent) {
        const bool fat = sdKind == FsKind::Fat;
        out.push_back(DeviceSlot{"sd", false, 0, sdKind, fat, fat ? SkipReason::None : unsupported(sdKind), false});
    }
    u32 count = 0;
    for (const PartitionInfo& p : usbParts) {
        DeviceSlot s{"", true, p.lba, p.kind, false, SkipReason::None, false};
        if (p.beyondLimit) {
            s.skip = SkipReason::BeyondLimit;  // libfat und BlockDevice adressieren mit u32
        } else if (!mountable(p.kind, support)) {
            s.skip = unsupported(p.kind);
        } else if (count >= kMaxUsbMounts) {
            s.skip = SkipReason::TooMany;
        } else {
            ++count;
            s.name = count == 1 ? "usb" : "usb" + std::to_string(count);
            s.mount = true;
            s.readOnly = p.kind != FsKind::Fat;
        }
        out.push_back(std::move(s));
    }
    return out;
}

const char* skipReasonText(SkipReason r, bool german) {
    switch (r) {
        case SkipReason::None: return "";
        case SkipReason::ExFat:
            return german ? "exFAT wird nicht unterstützt – bitte FAT32" : "exFAT is not supported – please use FAT32";
        case SkipReason::NtfsUnsupported: return german ? "NTFS wird nicht unterstützt" : "NTFS is not supported";
        case SkipReason::ExtUnsupported: return german ? "ext wird nicht unterstützt" : "ext is not supported";
        case SkipReason::WbfsUnsupported:
            return german ? "WBFS-Partition noch nicht unterstützt" : "WBFS partition not supported yet";
        case SkipReason::UnknownFs: return german ? "Dateisystem unbekannt" : "unknown file system";
        case SkipReason::BeyondLimit:
            return german ? "hinter der 32-Bit-Sektorgrenze (2 TiB)" : "beyond the 32-bit sector limit (2 TiB)";
        case SkipReason::TooMany: return german ? "zu viele Partitionen" : "too many partitions";
    }
    return "";
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
