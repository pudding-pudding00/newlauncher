#pragma once
// Kanalstart (Spec 6.6, VC-Inject): startet einen installierten Titel über das System-Menü-Startverfahren.

#include "core/result.hpp"
#include "core/types.hpp"

namespace nl::launch {

// Vorher: Laufwerke ausgehängt, Video schwarz. Kehrt nur zurück, wenn der Start scheitert.
Result<void> launchChannel(u64 titleId);

}  // namespace nl::launch
