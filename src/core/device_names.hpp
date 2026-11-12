#pragma once
// Gerätenamen für SD und USB-Partitionen (Spec v2 1, M12): "sd", "usb", "usb2" … "usb8".

#include "core/partition_table.hpp"
#include "core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace nl {

inline constexpr u32 kMaxUsbMounts = 8;

struct DeviceSlot {
    std::string name;    // ohne Doppelpunkt; leer, wenn die Partition nicht eingehängt wird
    bool usb;
    u64 lba;             // Partitionsanfang; 0 bei der SD (libfat sucht dort selbst)
    FsKind kind;
    bool mount;
    std::string reason;  // warum nicht eingehängt (fürs Log)
};

// SD (falls vorhanden) → "sd"; USB-Partitionen in Tabellenreihenfolge: erste FAT → "usb", weitere → "usb2"…"usb8".
// Andere Dateisysteme und FAT-Partitionen über die achte hinaus bleiben ohne Namen, mit Grund.
std::vector<DeviceSlot> assignDevices(bool sdPresent, FsKind sdKind, std::span<const PartitionInfo> usbParts);

const char* fsKindName(FsKind kind);

}  // namespace nl
