#pragma once
// M26: aus dem aufgelösten Startziel die Spielstand-Abfrage für locateSave bilden.

#include <optional>
#include <string_view>

#include "core/emulators.hpp"
#include "core/game.hpp"
#include "core/launch_plan.hpp"
#include "core/save_locator.hpp"

namespace nl {

// nullopt: keine Einheit möglich (echte GC-Speicherkarte, GC von Disc, EmuNAND-Kanal vor dem Plan, Homebrew).
std::optional<SaveQuery> saveQueryFor(const Game& g, const LaunchTarget& t, std::string_view libRoot,
                                      const EmulatorTable& emus);

}  // namespace nl
