#include "core/console.hpp"

namespace nl {

namespace {
const ConsoleInfo kConsoles[kConsoleCount] = {
    {Console::Nes, "nes", "NES", "Nintendo Entertainment System", 3, false},
    {Console::Snes, "snes", "SNES", "Super Nintendo", 4, false},
    {Console::Gb, "gb", "GB", "Game Boy", 4, true},
    {Console::N64, "n64", "N64", "Nintendo 64", 5, false},
    {Console::Gbc, "gbc", "GBC", "Game Boy Color", 5, true},
    {Console::Gc, "gc", "GC", "GameCube", 6, false},
    {Console::Gba, "gba", "GBA", "Game Boy Advance", 6, true},
    {Console::Wii, "wii", "Wii", "Wii", 7, false},
};
}  // namespace

const ConsoleInfo& consoleInfo(Console c) {
    const auto i = static_cast<unsigned>(c);
    return kConsoles[i < static_cast<unsigned>(kConsoleCount) ? i : 0];
}

}  // namespace nl
