#pragma once
// Gerätenamen für SD und USB-Partitionen (Spec v2 1, M12): "sd", "usb", "usb2" … "usb8".

#include "core/partition_table.hpp"
#include "core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace nl {

inline constexpr u32 kMaxUsbMounts = 8;

// Welche Dateisysteme außer FAT dieser Build einhängen kann (M16; Build-Schalter NL_FS_NTFS).
struct FsSupport {
    bool ntfs = false;
    bool ext = false;   // Spike M16 negativ: bleibt im Launcher false
    bool wbfs = false;  // M16 Task 5: Rohzugriff auf die Slots, kein Dateisystem
};

// Warum eine Partition keinen Namen bekommt (Log deutsch, Systeminfo DE/EN).
enum class SkipReason : u8 { None, ExFat, NtfsUnsupported, ExtUnsupported, WbfsUnsupported, UnknownFs, BeyondLimit, TooMany,
                              MountFailed };

struct DeviceSlot {
    std::string name;    // ohne Doppelpunkt; leer, wenn die Partition nicht eingehängt wird
    bool usb;
    u64 lba;             // Partitionsanfang; 0 bei der SD (libfat sucht dort selbst)
    FsKind kind;
    bool mount;
    SkipReason skip = SkipReason::None;
    bool readOnly = false;  // NTFS/ext: nur lesend eingehängt
};

// SD (falls vorhanden) → "sd"; USB: zuerst die FAT-Partitionen in Tabellenreihenfolge ("usb" ist die erste FAT-Partition,
// Spec v2 5.2, wie vor M16), danach die übrigen einhängbaren (NTFS/WBFS laut support) – zusammen höchstens
// "usb8". Andere bleiben ohne Namen, mit Grund.
std::vector<DeviceSlot> assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts,
                                      FsSupport support = {});

const char* skipReasonText(SkipReason r, bool german);

const char* fsKindName(FsKind kind);

}  // namespace nl
