#pragma once
// d2x-NAND-Emulation (M19, Spike docs/superpowers/spikes/m19-startwege.md).
// Portiert aus WiiFlow_Lite (nand.cpp, Zlib; channel_launcher.cpp, GPL-3.0-or-later) nach Gegenprüfung in d2x-cios.

#include <span>
#include <vector>

#include "core/emunand.hpp"
#include "core/types.hpp"

namespace nl::platform {

// Liest /sys/cert.sys vom echten NAND (vor IOS_ReloadIOS), falls ein EmuNAND eigene Zertifikate nicht mitbringt.
std::vector<u8> readRealNandCerts();

// Hängt FAT über d2x-fat-module (0xF0 SD / 0xF2 USB) ein und schaltet /dev/fs Ioctlv 100 ein.
// full = false: Partial-Modus (Spielstände für Wii-Spiele), full = true: voller EmuNAND (Kanäle).
bool enableNandEmu(const EmuNandPath& path, bool usb, s8 mbrIndex, bool full);

// Deaktiviert die NAND-Umleitung (Ioctl 100 = 0) und hängt das d2x-FAT-Gerät aus (0xF1 / 0xF3).
void disableNandEmu();

// Identifiziert einen Kanal im aktiven EmuNAND über TMD, Ticket und Zertifikate (ES_Identify).
bool identifyEmuChannel(u64 titleId, std::span<const u8> fallbackCerts);

}  // namespace nl::platform
