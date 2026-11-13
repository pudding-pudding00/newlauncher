#pragma once
// Emulator-Erkennung (Spec v2 6.3, M13 Task 3): <gerät>:/apps/*/ durchsuchen, Funde Profilen zuordnen und je
// Konsole den Emulator wählen. Vorrang: eigener emulators.ini-Abschnitt > games.ini [emulator] > Profilreihenfolge.
// Ein Abschnitt, der exakt der v1-Vorgabe entspricht, zählt nur, wenn seine Datei auch gefunden wurde.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/console.hpp"
#include "core/emulators.hpp"
#include "core/fs.hpp"
#include "core/types.hpp"

namespace nl {

inline constexpr std::size_t kMaxAppFolders = 256;            // je Gerät
inline constexpr std::size_t kMaxMetaXmlBytes = 64 * 1024;

struct FoundApp {
    std::string device;  // "sd", "usb2"
    std::string folder;  // Ordner unter apps/
    std::string dol;     // voller Pfad, z. B. "usb2:/apps/fceugx/boot.dol"
    std::string name;    // <name> aus meta.xml; leer, wenn keiner
};

struct EmuProfile {
    const char* id;        // "fceugx", "retroarch-fceumm"
    const char* folder;    // Ordner unter apps/
    const char* dolName;   // "boot.dol" oder "<kern>_libretro_wii.dol"
    const char* metaName;  // <name> aus meta.xml (erkennt umbenannte Ordner); nullptr = nur Ordner
    u8 consoleMask;        // Bit = Console-Wert
    const char* args;
    bool acceptsZip;
    u8 fsMask;  // M16: Dateisysteme, von denen der Emulator ROMs liest (fsBit); alle eingebauten nur FAT
};

std::span<const EmuProfile> builtinProfiles();
const EmuProfile* findProfile(std::string_view id);
// NES: fceugx, retroarch-fceumm; SNES: snes9xgx, retroarch-snes9x2010; GB/GBC: vbagx, retroarch-gambatte;
// GBA: vbagx, retroarch-mgba. N64, GC, Wii: leer (laufen über resolveTable).
std::span<const char* const> profileOrder(Console c);

struct ResolvedEmu {
    std::string dol;
    std::string args;
    std::string profile;      // leer bei eigenem INI-Pfad ohne Profil oder wenn nichts installiert ist
    bool fromIni = false;     // Pfad stammt aus einem Abschnitt in emulators.ini
    bool badProfile = false;  // gameProfile unbekannt oder für diese Konsole nicht zuständig (NL_OPT_BAD)
    u8 fsMask = 0;            // M16: fs aus dem INI-Abschnitt; 0 = Vorgabe des Profils
};

// nullopt nur für Konsolen ohne Emulator-Eintrag (N64, GC, Wii).
std::optional<ResolvedEmu> resolveEmulator(Console c, const EmulatorTable& ini, std::span<const FoundApp> found,
                                           std::optional<std::string_view> gameProfile);

// Ganze Tabelle aufgelöst: ROM-Konsolen, [n64_not64], [n64_wii64], [nintendont]. Einträge tragen profile.
EmulatorTable resolveTable(const EmulatorTable& ini, std::span<const FoundApp> found);

// Bit je ROM-Konsole: Profil mit acceptsZip; ohne Profil (eigener Pfad, nichts gefunden) das Standardbit.
u8 zipConsoles(const EmulatorTable& resolved);

// M16: fs aus emulators.ini, sonst die Maske des Profils, sonst nur FAT.
u8 emulatorFsMask(const EmulatorEntry& e);

bool isV1Default(std::string_view section, std::string_view dol);  // exakt der v1-Standardpfad

std::string metaXmlName(std::string_view xml);  // Inhalt von <name>…</name>, getrimmt; leer, wenn keiner

// Höchstens kMaxAppFolders Ordner; je Ordner boot.dol, boot.elf und *_libretro_wii.dol.
std::vector<FoundApp> scanApps(FileSystem& fs, std::string_view device);

}  // namespace nl
