#pragma once
// Mount von SD und USB über libfat (Spec 6.1, Spec v2 1). USB wird nicht blockierend alle 500 ms versucht,
// damit die Startszene „Warte auf USB …" weiter animiert. Jede FAT-Partition eines USB-Geräts bekommt einen
// eigenen Namen ("usb", "usb2" … "usb8", core/device_names).

#include "core/device_names.hpp"
#include "core/types.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nl::platform {

struct Device {
    DeviceSlot slot;
    bool mounted = false;
    bool writable = false;
    u64 totalBytes = 0;  // 0 = unbekannt (die SD nennt ihre Größe libogc nicht)
    u64 freeBytes = 0;   // 0 = unbekannt; libfat müsste dafür die ganze FAT lesen
};

class DeviceTable {
public:
    bool mountSd();   // "sd:"; true, wenn eingehängt
    bool mountUsb();  // liest die Partitionstabelle, hängt FAT (und mit NL_FS_NTFS NTFS nur lesend) ein; true, wenn "usb:" da ist
    // [debug] sd_as_disk (M16): mountUsb liest die Partitionstabelle der SD statt der USB-Platte und hängt
    // alles nur lesend ein (Dolphin emuliert keine USB-Platte). "sd:" bleibt unverändert.
    // Debug (M16-Smoke): wirkt nur, solange USB noch nicht eingehängt ist; sonst lesen RawDevice und Mount
    // verschiedene Geräte. Liefert, ob der Wert gilt.
    bool setSdAsDisk(bool on);
    bool sdAsDisk() const { return sdAsDisk_; }
    std::span<const Device> devices() const { return devices_; }
    const Device* byName(std::string_view name) const;  // "usb2" oder "usb2:"
    std::string root(u8 index) const;                    // "usb2:"; leer, wenn nicht eingehängt
    void unmountAll();

private:
    std::vector<Device> devices_;
    bool usbLogged_ = false;
    bool sdAsDisk_ = false;
};

DeviceTable& deviceTable();

bool mountSd();  // deviceTable().mountSd()

class UsbMount {
public:
    enum class State { Waiting, Ready, TimedOut };
    void begin(int timeoutSec);
    State poll(float dtMs);  // deviceTable().mountUsb()
    int secondsLeft() const;

private:
    State state_ = State::Waiting;
    float elapsedMs_ = 0.0f, nextTryMs_ = 0.0f, timeoutMs_ = 0.0f;
};

void unmountAll();  // deviceTable().unmountAll()

}  // namespace nl::platform
