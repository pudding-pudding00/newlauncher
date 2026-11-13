#pragma once
// Auswahl der Gecko-Codes eines Spiels: <data>/codes/<ID6>.sel, ein Codename pro Zeile (Spec v2 8).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/gecko_txt.hpp"

namespace nl {

std::vector<std::string> parseSel(std::string_view text);  // getrimmt, ohne Leerzeilen
std::string writeSel(std::span<const std::string> names);
// Gewählte Codes in Dateireihenfolge; Unbekanntes und Platzhalter fallen weg, ein doppelter Name zählt einmal.
std::vector<const GeckoCode*> selectedCodes(const GeckoFile& f, std::span<const std::string> names);

}  // namespace nl
