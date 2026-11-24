#include "core/console.hpp"

namespace nl {

namespace {
const ConsoleInfo kConsoles[kConsoleCount] = {
    {Console::Nes, "nes", "NES", "Nintendo Entertainment System", 3, false, {"roms/nes", nullptr}, {".nes"}},
    {Console::Snes, "snes", "SNES", "Super Nintendo", 4, false, {"roms/snes", nullptr}, {".sfc", ".smc"}},
    {Console::Gb, "gb", "GB", "Game Boy", 4, true, {"roms/gb", nullptr}, {".gb"}},
    {Console::N64, "n64", "N64", "Nintendo 64", 5, false, {"roms/n64", nullptr}, {".z64", ".n64", ".v64"}},
    {Console::Gbc, "gbc", "GBC", "Game Boy Color", 5, true, {"roms/gbc", nullptr}, {".gbc"}},
    {Console::Gc, "gc", "GC", "GameCube", 6, false, {"games", "gc"}, {".iso", ".gcm", ".ciso"}},
    {Console::Gba, "gba", "GBA", "Game Boy Advance", 6, true, {"roms/gba", nullptr}, {".gba"}},
    {Console::Wii, "wii", "Wii", "Wii", 7, false, {"wbfs", "wii"}, {".wbfs", ".iso"}},
    {Console::Channel, "channel", "Kanäle", "Kanäle", 8, false, {}, {}},
};
}  // namespace

const ConsoleInfo& consoleInfo(Console c) {
    const auto i = static_cast<unsigned>(c);
    return kConsoles[i < static_cast<unsigned>(kConsoleCount) ? i : 0];
}

}  // namespace nl
