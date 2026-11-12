#include "platform/storage.hpp"

#include <fat.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>

#include "core/partition_table.hpp"
#include "platform/log.hpp"
#include "platform/wii_disc.hpp"

namespace nl::platform {

namespace {
constexpr float kRetryMs = 500.0f;
constexpr u32 kCachePages = 8;  // je Partition 8 × 64 Sektoren Cache
constexpr u32 kSectorsPerPage = 64;

void logSlot(const Device& d) {
    const DeviceSlot& s = d.slot;
    if (d.mounted)
        log::line("NL_DEV %s %s mounted lba=%llu", s.name.c_str(), fsKindName(s.kind), static_cast<unsigned long long>(s.lba));
    else
        log::line("NL_DEV %s %s skipped lba=%llu (%s)", s.name.empty() ? "-" : s.name.c_str(), fsKindName(s.kind),
                  static_cast<unsigned long long>(s.lba), s.reason.empty() ? "Mount fehlgeschlagen" : s.reason.c_str());
}

bool isUsbMounted(const std::vector<Device>& devices) {
    for (const Device& d : devices)
        if (d.slot.usb && d.mounted) return true;
    return false;
}
}  // namespace

DeviceTable& deviceTable() {
    static DeviceTable table;
    return table;
}

bool DeviceTable::mountSd() {
    if (const Device* d = byName("sd"); d != nullptr && d->mounted) return true;
    const bool ok = fatMountSimple("sd", &__io_wiisd);
    const bool present = ok || __io_wiisd.isInserted();
    // libfat sucht die FAT-Partition der SD selbst; was dort sonst liegt, sieht der Launcher nicht.
    const auto slots = assignDevices(present, ok ? FsKind::Fat : FsKind::Unknown, {});
    for (const DeviceSlot& s : slots) {
        Device d{s, ok, ok, 0, 0};
        if (!ok) d.slot.mount = false;
        logSlot(d);
        devices_.insert(devices_.begin(), std::move(d));
    }
    return ok;
}

bool DeviceTable::mountUsb() {
    if (isUsbMounted(devices_)) return true;
    if (!__io_usbstorage.startup() || !__io_usbstorage.isInserted()) return false;

    RawDevice raw(true);
    auto parts = readPartitions(raw, 0);  // libogc nennt die Gerätegröße nicht
    if (!parts.isOk()) {
        if (!usbLogged_) log::line("NL_DEV usb table %s", parts.error().context.c_str());
        usbLogged_ = true;
        return false;
    }
    std::vector<Device> found;
    for (const DeviceSlot& s : assignDevices(false, FsKind::Unknown, parts.value())) {
        Device d{s, false, false, 0, 0};
        if (s.mount) d.mounted = d.writable = fatMount(s.name.c_str(), &__io_usbstorage, s.lba, kCachePages, kSectorsPerPage);
        for (const PartitionInfo& p : parts.value())
            if (p.lba == s.lba) d.totalBytes = p.sectors * raw.sectorSize();
        found.push_back(std::move(d));
    }
    const bool ok = isUsbMounted(found);
    if (ok || !usbLogged_)
        for (const Device& d : found) logSlot(d);
    usbLogged_ = true;
    if (!ok) {
        for (const Device& d : found)
            if (d.mounted) fatUnmount(d.slot.name.c_str());
        return false;
    }
    for (Device& d : found) devices_.push_back(std::move(d));
    return true;
}

const Device* DeviceTable::byName(std::string_view name) const {
    if (!name.empty() && name.back() == ':') name.remove_suffix(1);
    for (const Device& d : devices_)
        if (!d.slot.name.empty() && d.slot.name == name) return &d;
    return nullptr;
}

std::string DeviceTable::root(u8 index) const {
    if (index >= devices_.size() || !devices_[index].mounted) return {};
    return devices_[index].slot.name + ":";
}

void DeviceTable::unmountAll() {
    // USB zuerst, wie bisher; die SD trägt das Log.
    for (auto it = devices_.rbegin(); it != devices_.rend(); ++it)
        if (it->mounted) fatUnmount(it->slot.name.c_str());
    devices_.clear();
    usbLogged_ = false;
}

bool mountSd() { return deviceTable().mountSd(); }

void UsbMount::begin(int timeoutSec) {
    state_ = State::Waiting;
    elapsedMs_ = 0.0f;
    nextTryMs_ = 0.0f;
    timeoutMs_ = static_cast<float>(timeoutSec) * 1000.0f;
}

UsbMount::State UsbMount::poll(float dtMs) {
    if (state_ != State::Waiting) return state_;
    if (elapsedMs_ >= nextTryMs_) {
        nextTryMs_ += kRetryMs;
        if (deviceTable().mountUsb()) {
            state_ = State::Ready;
            return state_;
        }
    }
    elapsedMs_ += dtMs;
    if (elapsedMs_ >= timeoutMs_) state_ = State::TimedOut;
    return state_;
}

int UsbMount::secondsLeft() const {
    const float left = timeoutMs_ - elapsedMs_;
    return left <= 0.0f ? 0 : static_cast<int>((left + 999.0f) / 1000.0f);
}

void unmountAll() { deviceTable().unmountAll(); }

}  // namespace nl::platform
