#pragma once
// Liest Wiimotes (inkl. Nunchuk und IR-Zeiger) und GameCube-Pads in ein geräteneutrales RawInput.

#include "ui/input.hpp"

namespace nl::platform {

class WiiInput {
public:
    void init(float logicalWidth, float logicalHeight);
    ui::RawInput poll();
    void shutdown();

private:
    bool initialized_ = false;
};

}  // namespace nl::platform
