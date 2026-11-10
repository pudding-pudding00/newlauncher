#pragma once
// Chainloader (Spec 9, M3): legt Ziel-DOL, argv-Block und App-Booter in den reservierten MEM2-Bereich
// (launch_layout.hpp), meldet den Launcher ab und springt in den Booter.

#include "core/fs.hpp"
#include "core/launch_plan.hpp"
#include "core/result.hpp"

namespace nl::launch {

// Liest plan.dolPath vollständig nach kDolBufferAddr; InvalidData, wenn weniger als plan.dolSize kam.
Result<void> loadDolToBuffer(FileSystem& fs, const LaunchPlan& plan);

// Vorher: loadDolToBuffer erfolgreich, Laufwerke ausgehängt, Video schwarz. Kehrt nie zurück.
[[noreturn]] void chainload(const LaunchPlan& plan);

}  // namespace nl::launch
