#pragma once
// Spieloptionen (Spec 7): globale Standards in config.ini [wii]/[gc], pro Spiel in games.ini [game <ID6>].
// Vorrang Spiel > global > Standard; „default“ im Spiel heißt global. Schlüssel, Werte und der Abschnittsname
// ohne Groß-/Kleinschreibung, getrimmt; bei mehrfachen Einträgen gilt der letzte. Unbekannte Werte nehmen den
// Standard und landen in `bad` (der Aufrufer loggt NL_OPT_BAD <schlüssel>).

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/console.hpp"
#include "core/ini.hpp"
#include "core/nin_cfg.hpp"
#include "core/types.hpp"

namespace nl {

// Plattformfreies Spiegelbild der the_CFG-Felder, die Optionen setzen (boot/include/booter_cfg.h).
struct WiiCfgFields {
    u8 vidMode = 1;          // game/system/pal50/pal60/ntsc/progressive → 0–5
    u8 patchVidMode = 0;     // off/normal/more/all → 0–3
    u8 vipatch = 0;
    u8 language = 0xCD;      // configbytes[0]: 0xCD = Spiel, sonst ja … ko → 0–9
    u8 countryString = 0;
    u8 patchregion = 0;
    s8 aspectRatio = -1;     // game/4:3/16:9 → −1/0/1
    u8 deflicker = 0;        // game/off/off_ext/low/medium/high → 0–5
    u8 videoWidth = 0;       // auto/framebuffer → 0/1
    u8 patchFix480p = 0;
    u8 privateServer = 0;    // off/nossl/wiimmfi → 0–2
    u32 returnTo = 0;        // untere Hälfte der Kanal-Title-ID (Booter: u32)
};

inline constexpr u32 kReturnToHbc = 0x4C554C5A;   // "LULZ", HBC ≥ 1.1
inline constexpr u32 kReturnToHbcOld = 0x4A4F4449;  // "JODI", ältere HBC

// return_to = hbc: LULZ, wenn installiert, sonst JODI, sonst 0 (keine Rückkehr).
u32 hbcReturnTo(std::span<const u64> installedTitles);

struct WiiOptions {
    WiiCfgFields cfg;
    std::optional<u8> ios;         // nullopt = auto
    bool returnToHbc = false;      // return_to = hbc: die Plattform darf auf JODI ausweichen
    bool cheats = false;           // M15: ausgewählte Gecko-Codes anwenden
    u8 hookType = 1;               // vbi/kpad/joypad/gxdraw/gxflush/ossleep/axframe → 1–7 (0 = ohne Cheats)
    std::vector<std::string> bad;  // Schlüssel mit unbekanntem Wert
};

struct GcOptions {
    NinConfigInput nin{{}, {}, false};  // gamePath, id6, usb setzt der Aufrufer
    bool memcardReal = false;
    bool nativeSi = false;
    bool cheats = false;  // M15: Nintendont lädt die GCT (NIN_CFG_CHEATS + CheatPath)
    std::vector<std::string> unsupported;  // gesetzte Optionen, die die Nintendont-Version nicht kennt
    std::vector<std::string> bad;
};

WiiOptions resolveWiiOptions(const IniDoc& config, const IniDoc& games, std::string_view id6);
GcOptions resolveGcOptions(const IniDoc& config, const IniDoc& games, std::string_view id6, NinVersion nin);

// Eine Zeile der Options-Szene. Ohne id6 (globale Standards) ist nichts geerbt.
struct OptionValue {
    std::string_view key;
    std::string value;           // wirksamer Wert, normalisiert (klein)
    bool inherited = false;      // kein Wert im Spiel (oder „default“)
    std::string inheritedValue;  // globaler Wert bzw. Standard
};
// Wii 15, GC 11 Zeilen in Tabellenreihenfolge der Spec; andere Konsolen: leer.
std::vector<OptionValue> listOptions(Console c, const IniDoc& config, const IniDoc& games, std::string_view id6);

// Erlaubte Werte einer Option in Anzeigereihenfolge (für die Szene); ios und return_to: nur die festen Werte.
std::vector<std::string_view> optionValues(Console c, std::string_view key);

}  // namespace nl
