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

// games.ini. Nur [n64_method] wird gelesen und geschrieben; andere Abschnitte gehen beim Speichern verloren.
class GameOverrides {
public:
    static GameOverrides fromIni(const IniDoc& doc);
    std::optional<N64Method> n64Method(std::string_view key) const;
    void setN64Method(std::string_view key, std::optional<N64Method> m);  // nullopt löscht
    IniDoc toIni() const;
    std::size_t size() const { return n64_.size(); }

private:
    std::vector<std::pair<std::string, N64Method>> n64_;  // Einfügereihenfolge
};

}  // namespace nl
