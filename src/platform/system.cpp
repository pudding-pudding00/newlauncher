#include "platform/system.hpp"

#include <ogc/machine/processor.h>

namespace nl::platform {

namespace {
constexpr unsigned kHwAhbprot = 0x0D800064;  // Hollywood: AHB-Schutz; alle Bits gesetzt = Zugriff frei
}

bool ahbprotDisabled() { return read32(kHwAhbprot) == 0xFFFFFFFFu; }

}  // namespace nl::platform
