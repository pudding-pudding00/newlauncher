#pragma once
// Bibliothek über mehrere Geräte (Spec v2 5.2): ein Index je Gerät, im Speicher zusammengeführt.

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/block_dev.hpp"
#include "core/fs.hpp"
#include "core/index_file.hpp"
#include "core/meta_db.hpp"
#include "core/scanner.hpp"

namespace nl {

struct DeviceIndex {
    std::string device;  // "sd", "usb", "usb2" …
    IndexData index;
};

struct MergedGame {
    IndexGame game;
    u8 device = 0;            // Index in MergedLibrary::devices
    std::vector<u8> alsoOn;   // weitere Geräte mit demselben Schlüssel, in Quellenreihenfolge
};

struct MergedLibrary {
    std::vector<std::string> devices;  // Quellenreihenfolge
    std::vector<MergedGame> games;     // sortiert nach Konsole, dann Titel
};

// Gleicher Schlüssel auf mehreren Geräten: das erste Gerät der Liste gewinnt.
MergedLibrary mergeIndexes(std::span<const DeviceIndex> inOrder);

// "sd" → "SD", "usb" → "USB 1", "usb2" → "USB 2"; Unbekanntes in Großbuchstaben.
std::string deviceLabel(std::string_view device);

// Wurzel für Cover- und Index-Dateien eines Geräts, passend für coverSrcPath/coverTexPath:
// beschreibbar → "usb2:", sonst "<data>/cache/usb2" (darunter dieselbe Ordnerstruktur wie auf dem Gerät).
std::string coverRoot(std::string_view device, bool writable, std::string_view dataDir);
// Ordner von index.bin: coverRoot(…) + "/newlauncher/cache".
std::string deviceCacheDir(std::string_view device, bool writable, std::string_view dataDir);

struct MultiScan {
    std::vector<DeviceIndex> indexes;  // nur lesbare Geräte, in der Reihenfolge von `devices`
    ScanStats stats;                   // Summe über alle Geräte
    std::vector<std::pair<std::string, u32>> wbfsSkips;  // (Gerät, Slot) übersprungener WBFS-Slots
};

// Gerät ohne Dateisystem (WBFS-Partition, M16): gelesen über Rohzugriff ab partLba (Gerätesektoren).
struct RawSource {
    std::string device;
    BlockDevice* dev;
    u64 partLba;
};

// Scannt jedes Gerät mit seinem vorherigen Index (gleicher Gerätename). Nicht lesbare Geräte fallen weg;
// ist keines lesbar: NotMounted. Abbruch über progress: Cancelled.
Result<MultiScan> scanDevices(FileSystem& fs, std::span<const std::string> devices, const ScanRules& rules,
                              std::span<const DeviceIndex> previous, const MetaDb* meta,
                              ScanProgress progress = nullptr, void* user = nullptr,
                              std::span<const RawSource> raw = {});

}  // namespace nl
