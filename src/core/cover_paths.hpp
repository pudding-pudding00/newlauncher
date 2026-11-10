#pragma once
// Cover-Pfade (Spec 5.1, 5.2): Stamm "<konsole>/<ID6>" bzw. "<konsole>/<Dateiname ohne Endung>".

#include <string>
#include <string_view>

#include "core/game.hpp"

namespace nl {

std::string coverStem(const Game& game);
std::string coverSrcPath(std::string_view libRoot, std::string_view stem);  // <lib>/newlauncher/covers_src/<stem>.png
std::string coverTexPath(std::string_view libRoot, std::string_view stem);  // <lib>/newlauncher/cache/covers/<stem>.tex

}  // namespace nl
