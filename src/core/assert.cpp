#include "core/assert.hpp"

#include <cstdlib>

namespace nl {

namespace {
PanicHandler g_handler = nullptr;
}

void setPanicHandler(PanicHandler handler) { g_handler = handler; }

void panic(const char* expr, const char* file, int line) {
    if (g_handler != nullptr) g_handler(expr, file, line);
    // Ein Handler darf nicht zurückkehren; tut er es doch, endet das Programm hier.
    std::abort();
}

}  // namespace nl
