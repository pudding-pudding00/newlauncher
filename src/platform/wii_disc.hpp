#pragma once
// Wii-Spiele (M9 Task 5): Rohsektoren der Bibliothek, Fragmentliste mit Gegenprobe über libfat und
// Zustand des cIOS in Slot 249. Nur auf der Wii.

#include "core/block_dev.hpp"
#include "core/frag_list.hpp"
#include "core/fs.hpp"
#include "core/game.hpp"
#include "core/ios_info.hpp"
#include "core/launch_plan.hpp"

#include <string_view>

struct DISC_INTERFACE_STRUCT;

namespace nl::platform {

// Liest über das DISC_INTERFACE von libfat ("sd:" → __io_wiisd, "usb:" → __io_usbstorage) durch einen
// 32-Byte-ausgerichteten Zwischenpuffer mit 64 Sektoren.
class RawDevice final : public BlockDevice {
public:
    explicit RawDevice(bool usb);
    ~RawDevice() override;
    RawDevice(const RawDevice&) = delete;
    RawDevice& operator=(const RawDevice&) = delete;

    bool read(u32 lba, u32 count, std::span<u8> out) override;
    u32 sectorSize() const override { return sectorSize_; }

private:
    const DISC_INTERFACE_STRUCT* io_;
    u32 sectorSize_;
    u8* buf_;
};

// Teile sammeln (Hauptdatei, bei .wbfs dazu .wbf1 … .wbf9), Extents je Teil, Liste bauen und gegenprüfen:
// erster und letzter Sektor des ersten und des letzten Teils roh gegen fs.read. Loggt NL_FRAG_VERIFY.
Result<FragList, LaunchError> computeWiiFrags(FileSystem& fs, const Game& g, std::string_view libRoot, u32* partLbaOut);

// TMD von 00000001-000000F9 und Kopf des Boot-Contents (d2x: ios_info_t).
CiosStatus readCiosStatus();

}  // namespace nl::platform
