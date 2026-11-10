#include "core/console.hpp"

namespace nl {

namespace {
const ConsoleInfo kConsoles[kConsoleCount] = {
    {Console::Nes, "nes", "NES", "Nintendo Entertainment System", 3, false, "roms/nes", {".nes", nullptr, nullptr}},
    {Console::Snes, "snes", "SNES", "Super Nintendo", 4, false, "roms/snes", {".sfc", ".smc", nullptr}},
    {Console::Gb, "gb", "GB", "Game Boy", 4, true, "roms/gb", {".gb", nullptr, nullptr}},
    {Console::N64, "n64", "N64", "Nintendo 64", 5, false, "roms/n64", {".z64", ".n64", ".v64"}},
    {Console::Gbc, "gbc", "GBC", "Game Boy Color", 5, true, "roms/gbc", {".gbc", nullptr, nullptr}},
    {Console::Gc, "gc", "GC", "GameCube", 6, false, "games", {".iso", nullptr, nullptr}},
    {Console::Gba, "gba", "GBA", "Game Boy Advance", 6, true, "roms/gba", {".gba", nullptr, nullptr}},
    {Console::Wii, "wii", "Wii", "Wii", 7, false, "wbfs", {".wbfs", ".iso", nullptr}},
};
}  // namespace

const ConsoleInfo& consoleInfo(Console c) {
    const auto i = static_cast<unsigned>(c);
    return kConsoles[i < static_cast<unsigned>(kConsoleCount) ? i : 0];
}

}  // namespace nl
