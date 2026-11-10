#include "core/cover_paths.hpp"

namespace nl {

std::string coverStem(const Game& game) {
    std::string stem = consoleInfo(game.console).key;
    stem += '/';
    std::string_view key = game.key ? game.key : "";
    if (game.console == Console::Wii || game.console == Console::Gc) {
        stem += key;
        return stem;
    }
    if (const auto slash = key.find('/'); slash != std::string_view::npos) key.remove_prefix(slash + 1);
    if (const auto dot = key.rfind('.'); dot != std::string_view::npos && dot > 0) key = key.substr(0, dot);
    stem += key;
    return stem;
}

std::string coverSrcPath(std::string_view libRoot, std::string_view stem) {
    std::string p(libRoot);
    p += "/newlauncher/covers_src/";
    p += stem;
    p += ".png";
    return p;
}

std::string coverTexPath(std::string_view libRoot, std::string_view stem) {
    std::string p(libRoot);
    p += "/newlauncher/cache/covers/";
    p += stem;
    p += ".tex";
    return p;
}

}  // namespace nl
