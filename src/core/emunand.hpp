#pragma once
// EmuNAND (M19, Spec v2 12.2): Pfadprüfung, Kanal-Scan über title.tmd + IMET sowie d2x-Tauglichkeitsprüfung.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/device_names.hpp"
#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"
#include "core/wii_title.hpp"

namespace nl {

inline constexpr std::size_t kMaxEmuNandFolderLen = 64;
inline constexpr std::size_t kMaxEmuTitles = 512;

struct EmuNandPath {
    std::string device;  // "sd", "usb", "usb2"... (klein, ohne ':')
    std::string folder;  // "/emunand" (mit führendem '/', ohne Ende-Slash, ≤ 64 Zeichen)
};

// Parst "<gerät>:/<ordner>", kappt abschließende '/' am Ordner, lehnt ".." und > 64 Zeichen ab.
Result<EmuNandPath> parseEmuNandPath(std::string_view v);

struct EmuTitle {
    u64 titleId = 0;
    std::string name;
    u32 content0 = 0;
    std::string content0Path;  // "<gerät>:/<ordner>/title/00010001/<low8>/content/<c0>.app"
};

enum class EmuNandStatus : u8 { NotConfigured, InvalidPath, DeviceMissing, NotFat, NotPrimaryMbr, MissingTitleDir, Ok };

struct EmuNandInfo {
    EmuNandStatus status = EmuNandStatus::NotConfigured;
    EmuNandPath path;
    bool usb = false;
    s8 mbrIndex = -1;
    std::string detail;  // Gerätename (z. B. "USB 2") oder Pfad für Fehler- und Systeminfo-Texte
};

// Prüft, ob [emunand] path gültig ist, auf einem eingehängten FAT-Gerät mit primärer MBR-Partition (oder SD)
// liegt und einen "title"-Ordner enthält.
EmuNandInfo checkEmuNand(FileSystem& fs, std::string_view rawPath, std::span<const DeviceSlot> devices);

// Durchsucht <dev>:<folder>/title/00010001/*/content/title.tmd + Content-0-IMET (max. kMaxEmuTitles Titel).
std::vector<EmuTitle> scanEmuNand(FileSystem& fs, const EmuNandPath& p, BannerLang lang, u32* skipped = nullptr);

// Klartext-Statuszeile für Systeminfo (DE/EN).
std::string emuNandStatusText(const EmuNandInfo& info, u32 titleCount, bool german);

// Parst genau 16 Hex-Zeichen als 64-Bit-Title-ID.
std::optional<u64> parseTitleIdHex(std::string_view hex);

}  // namespace nl
