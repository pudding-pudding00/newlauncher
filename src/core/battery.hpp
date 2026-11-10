#pragma once
// Akkustand der Wiimotes (Spec 3.3): WPADData::battery_level 0..200 (0xC8 = voll) in 0..4 Balken.

#include <algorithm>

#include "core/types.hpp"

namespace nl {

constexpr int batteryBars(u8 raw) { return std::min(4, (raw * 4 + 199) / 200); }

}  // namespace nl
