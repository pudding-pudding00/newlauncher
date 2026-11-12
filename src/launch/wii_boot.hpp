#pragma once
// Start eines Wii-Spiels (M9 Task 6): the_CFG und Fragmentliste in den reservierten MEM2-Bereich, den
// eingebetteten Game-Booter in den DOL-Puffer, dann IOS 249 laden und über den M3-Chainloader springen.

#include "core/launch_plan.hpp"
#include "core/result.hpp"

#include <span>
#include <string_view>

namespace nl::launch {

// Wie loadDolToBuffer, aber aus dem Speicher: 0 < Größe, gültiger Kopf, und die DOL endet vor NL_WII_CFG_ADDR.
Result<void> loadDolBytesToBuffer(std::span<const u8> dol);

// Schreibt the_CFG (Standardwerte aus Plan M9) und die Liste, lädt den Booter. Liefert den Plan für
// bootWiiGame (dolSize gesetzt, args = {"game_booter"}). Noch nichts ist abgemeldet; ein Fehler lässt
// den Launcher bedienbar.
Result<LaunchPlan> prepareWiiBoot(const WiiBootPlan& plan, std::string_view title);

// Vorher: prepareWiiBoot erfolgreich, Laufwerke ausgehängt, Video schwarz. Lädt das cIOS in iosSlot
// (WiiBootPlan::iosSlot); ist danach ein anderes IOS aktiv, zurück ins System-Menü. Kehrt nie zurück.
[[noreturn]] void bootWiiGame(const LaunchPlan& booter, u8 iosSlot);

}  // namespace nl::launch
