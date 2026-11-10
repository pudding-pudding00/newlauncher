#pragma once
// Liest Wiimotes (inkl. Nunchuk und IR-Zeiger) und GameCube-Pads in ein geräteneutrales RawInput.

#include <array>

#include "core/types.hpp"
#include "ui/input.hpp"

namespace nl::platform {

class WiiInput {
public:
    void init(float logicalWidth, float logicalHeight);
    ui::RawInput poll();
    void shutdown();
    // Wiimote P1..P4: -1 = nicht verbunden, sonst 0..4 Balken; poll() aktualisiert alle 60 Frames.
    std::array<s8, 4> battery() const { return battery_; }

private:
    bool initialized_ = false;
    std::array<s8, 4> battery_{-1, -1, -1, -1};
    u32 pollCount_ = 0;
};

}  // namespace nl::platform
