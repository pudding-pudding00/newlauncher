#pragma once
// N64-Startmethoden (Spec 2, 6.6) und die Overrides pro Spiel aus games.ini, Abschnitt [n64_method].

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ini.hpp"
#include "core/types.hpp"

namespace nl {

enum class N64Method : u8 { Vc, Not64, Wii64 };
inline constexpr int kN64MethodCount = 3;

const char* n64MethodKey(N64Method m);    // "vc" | "not64" | "wii64" (Dateiwerte)
const char* n64MethodLabel(N64Method m);  // "VC-Inject" | "Not64" | "Wii64" (Anzeige)
std::optional<N64Method> parseN64Method(std::string_view s);  // getrimmt, Groß/klein egal

// games.ini. [n64_method] wird gelesen und geschrieben; aus [game <ID6>] liest M13 den Schlüssel ios (Spec v2
// 6.2/7.1). Alle anderen Abschnitte und Schlüssel bleiben beim Speichern erhalten (ohne Kommentare).
class GameOverrides {
public:
    static GameOverrides fromIni(const IniDoc& doc);
    std::optional<N64Method> n64Method(std::string_view key) const;
    void setN64Method(std::string_view key, std::optional<N64Method> m);  // nullopt löscht
    IniDoc toIni() const;
    std::size_t size() const { return n64_.size(); }

    // [game <id6>] ios: 200–255; "auto", fehlend oder ungültig → nullopt. Ungültige stehen in badKeys().
    std::optional<u8> wiiIos(std::string_view id6) const;
    const std::vector<std::string>& badKeys() const { return bad_; }  // "game <ID6>.ios" für NL_OPT_BAD

private:
    std::vector<std::pair<std::string, N64Method>> n64_;  // Einfügereihenfolge
    std::vector<std::pair<std::string, u8>> wiiIos_;      // ID6 → Slot
    std::vector<std::pair<std::string, std::vector<std::string>>> others_;  // fremde Abschnitte, Rohzeilen
    std::vector<std::string> bad_;
};

}  // namespace nl
