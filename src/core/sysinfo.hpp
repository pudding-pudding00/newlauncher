#pragma once
// Systeminfo (Spec v2 6.1, M13 Task 4): was der Launcher über die Konsole weiß, als Bildschirmliste und als
// sysinfo.txt zum Weitergeben. Enthält bewusst keine MAC- und keine IP-Adresse.

#include <string>
#include <vector>

#include "core/cios_scan.hpp"
#include "core/emulators.hpp"
#include "core/types.hpp"

namespace nl {

enum class Platform : u8 { Wii, VWii, Unknown };

struct DeviceSummary {
    std::string name;  // "sd", "usb2"
    std::string fs;    // "FAT", "WBFS"
    u64 total = 0;     // 0 = unbekannt
    u64 free = 0;
    u32 games = 0;
};

struct ResolvedEmuSummary {
    std::string section;  // "nes", "n64_not64"
    std::string profile;  // leer = kein Profil
    std::string how;      // "ini", "detected", "default"
    std::string path;
};

struct SystemInfo {
    Platform platform = Platform::Unknown;
    u16 sysMenuVersion = 0;  // 0 = nicht lesbar
    char region = '?';       // 'U','E','J','K' oder '?'
    std::string launcherVersion;
    u32 launcherIos = 0;
    bool ahbprot = false;  // true = AHBPROT abgeschaltet, der Launcher hat direkten Hardwarezugriff
    std::vector<CiosEntry> cios;
    u8 ciosSlot = 0;       // gewählter Slot für Wii-Spiele; 0 = keiner
    bool gcPorts = false;  // false auf vWii; die Wii mini wird nicht erkannt (Spec 6.1)
    std::vector<DeviceSummary> devices;
    std::vector<ResolvedEmuSummary> emulators;
    bool netUp = false;
    bool timeSynced = false;
    bool tls = false;
};

// 4.0–4.3 und vWii (Versionstabelle im M13-Ledger); andere Versionen '?'.
char regionFromSysMenu(u16 version);
// 513 → "4.3U", 610 → "vWii E", 0 → "?", sonst "v<zahl>".
std::string sysMenuLabel(u16 version);
// vWii bei 608–610 oder erkanntem Latte-Chip; Version 0 ohne Latte → Unknown.
Platform platformForSysMenu(u16 version, bool latteDetected);

// nes, snes, gb, gbc, gba, n64_not64, n64_wii64, nintendont; Abschnitte ohne Pfad fehlen. how: "ini" (eigener
// Abschnitt), "detected" (Profil gefunden), "default" (v1-Vorgabe). Gleiche Quelle wie das Log NL_EMU.
std::vector<ResolvedEmuSummary> emuSummaries(const EmulatorTable& resolved);

std::vector<std::string> sysInfoLines(const SystemInfo& s, bool german);
std::string formatSysInfo(const SystemInfo& s, bool german);  // Zeilen mit '\n' abgeschlossen

}  // namespace nl
