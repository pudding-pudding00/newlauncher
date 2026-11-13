#pragma once
// Kindersicherung (M17, Spec v2 10.5). Ein Kinderschloss, kein Tresor: PIN mit 4 Ziffern als
// SHA-256(salt ‖ pin) mit 16 Byte Salz. Ohne gesetzte PIN ist die Kindersicherung aus. Die PIN wird nie geloggt.
//
// [parental]
//   pin_hash = <64 Hexziffern>   salt = <32 Hexziffern>
//   system = usk|pegi|esrb|cero  max_age = 0..18  lock_unrated = 0|1  use_wii = 0|1
// Sperre pro Spiel: games.ini [game <schlüssel>] locked = 1.

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ini.hpp"
#include "core/rating.hpp"
#include "core/sha256.hpp"

namespace nl {

struct ParentalConfig {
    std::array<u8, 32> pinHash{};
    std::array<u8, 16> salt{};
    bool enabled = false;  // = PIN gesetzt
    RatingSystem system = RatingSystem::Pegi;
    u8 maxAge = 18;  // 255 = keine Grenze (nur aus dem Wii-Jugendschutz)
    bool lockUnrated = false;
    bool useWii = false;  // Stufe und PIN aus SYSCONF IPL.PC
    u32 lockUntil = 0;    // Unix-Sekunden: PIN-Sperre läuft bis dahin, auch über einen Neustart (0 = keine)
    bool operator==(const ParentalConfig&) const = default;
};

ParentalConfig parentalFromIni(const IniDoc& ini);
std::vector<std::pair<std::string, std::string>> parentalIniValues(const ParentalConfig& c);  // Schlüssel in [parental]

bool validPin(std::string_view pin);  // genau 4 ASCII-Ziffern
bool checkPin(const ParentalConfig& c, std::string_view pin);
// Neue PIN mit neuem Salz; eine ungültige PIN lässt c unverändert.
ParentalConfig withPin(ParentalConfig c, std::string_view pin, std::span<const u8, 16> salt);
ParentalConfig withoutPin(ParentalConfig c);

// Gesperrt, wenn lockedFlag, oder bewertet im gewählten System mit age > maxAge, oder unbewertet und
// lockUnrated. Alter ist in Jahren normiert, daher gilt auch eine Bewertung aus einem anderen System. Ohne PIN nie gesperrt.
bool isLocked(const ParentalConfig& c, const Rating& r, bool lockedFlag);

// 3 falsche PINs → 30 s keine Eingabe (nicht die Konsole). Millisekunden-Uhr darf überlaufen.
class PinGate {
public:
    bool submit(bool ok, u32 nowMs);  // true = PIN angenommen
    bool blocked(u32 nowMs) const;
    u32 remainingMs(u32 nowMs) const;
    void blockFor(u32 ms, u32 nowMs);  // Sperre von außen (Restzeit nach Neustart); 0 = keine

private:
    u8 fails_ = 0;
    bool active_ = false;
    u32 until_ = 0;
};

// Zeilen der Kindersicherungs-Szene (M17). Pin und RemovePin lösen etwas aus, der Rest ändert einen Wert.
enum class ParentalRow : u8 { Pin, System, MaxAge, LockUnrated, UseWii, RemovePin, Count };
bool isParentalAction(ParentalRow r);
// System zyklisch USK → PEGI → ESRB → CERO; Altersgrenze zyklisch über 0, 3, 6, 7, 12, 13, 16, 17, 18; dir = ±1.
void stepParental(ParentalConfig& c, ParentalRow r, int dir);
// RemovePin nur mit gesetzter PIN; mit Wii-Jugendschutz gelten dessen PIN und Alter (Pin, MaxAge, RemovePin aus).
bool parentalRowEnabled(const ParentalConfig& c, ParentalRow r);

const char* ratingSystemName(RatingSystem s);  // "usk", "pegi", "esrb", "cero"; None → ""
std::optional<RatingSystem> parseRatingSystem(std::string_view s);

// Wii-Jugendschutz (Spike docs/superpowers/spikes/m17-sysconf-pc.md).
struct WiiParental {
    bool enabled = false;
    RatingSystem system = RatingSystem::None;  // None = unbekannte Bewertungsstelle
    u8 maxAge = 255;
    std::array<char, 4> pin{};
};
// Nutzdaten eines Eintrags in einer SYSCONF-Datei (bei Arrays ohne Längenfeld); nullopt bei kaputter Datei.
std::optional<std::span<const u8>> sysconfItem(std::span<const u8> file, std::string_view name);
std::optional<WiiParental> parseIplPc(std::span<const u8> payload);
// Wii-Stufe und -PIN ersetzen die eigenen; eine unbekannte Stelle behält c.system. Wii aus → Kindersicherung aus.
ParentalConfig applyWiiParental(ParentalConfig c, const WiiParental& w, std::span<const u8, 16> salt);

}  // namespace nl
