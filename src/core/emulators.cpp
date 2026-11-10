#include "core/emulators.hpp"

namespace nl {

namespace {

constexpr const char* kDefaultText =
    "[nes]\n"
    "dol = sd:/apps/fceugx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[snes]\n"
    "dol = sd:/apps/snes9xgx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gb]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gbc]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[gba]\n"
    "dol = sd:/apps/vbagx/boot.dol\n"
    "args = {dir}|{file}\n"
    "[n64_not64]\n"
    "dol = sd:/apps/not64/boot.dol\n"
    "args = {path}\n"
    "[n64_wii64]\n"
    "dol = sd:/apps/wii64/boot.dol\n"
    "args = {path}\n"
    "[nintendont]\n"
    "dol = sd:/apps/nintendont/boot.dol\n";

constexpr Console kRomConsoles[] = {Console::Nes, Console::Snes, Console::Gb, Console::Gbc, Console::Gba};

}  // namespace

std::string defaultEmulatorsText() { return kDefaultText; }

EmulatorTable EmulatorTable::fromIni(const IniDoc& doc) {
    const IniDoc defaults = IniDoc::parse(kDefaultText);
    EmulatorTable t;
    for (Console c : kRomConsoles) {
        const std::string_view key = consoleInfo(c).key;
        EmulatorEntry e;
        // Leeres dol wäre ein sicherer Fehlstart; leeres args ist dagegen gültig (nur argv[0]).
        const std::string_view dol = doc.get(key, "dol", "");
        e.dol = std::string(dol.empty() ? defaults.get(key, "dol") : dol);
        e.args = std::string(doc.get(key, "args", defaults.get(key, "args")));
        t.entries_[static_cast<std::size_t>(c)] = std::move(e);
    }
    return t;
}

const EmulatorEntry* EmulatorTable::forConsole(Console c) const {
    const auto& e = entries_[static_cast<std::size_t>(c)];
    return e ? &*e : nullptr;
}

}  // namespace nl
