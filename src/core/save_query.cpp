#include "core/save_query.hpp"

#include "core/nin_cfg.hpp"

namespace nl {
namespace {

std::string emuRoot(const EmuNandInfo& e) { return e.path.device + ":" + e.path.folder; }

std::string stemOf(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
    return std::string(name);
}

const EmulatorEntry* romEntry(const Game& g, const LaunchTarget& t, const EmulatorTable& emus) {
    if (g.console == Console::N64) return t.n64 ? emus.forN64(*t.n64) : nullptr;
    switch (g.console) {
        case Console::Nes:
        case Console::Snes:
        case Console::Gb:
        case Console::Gbc:
        case Console::Gba: return emus.forConsole(g.console);
        default: return nullptr;
    }
}

}  // namespace

std::optional<SaveQuery> saveQueryFor(const Game& g, const LaunchTarget& t, std::string_view libRoot,
                                      const EmulatorTable& emus) {
    const std::string_view key = g.key != nullptr ? std::string_view(g.key) : std::string_view{};
    SaveQuery q;
    switch (t.kind) {
        case LaunchTarget::Kind::Wii: {
            if (!t.wii) return std::nullopt;
            if (t.channelPlanned) {
                if (!t.wii->emuNand) return std::nullopt;
                q.kind = SaveKind::Channel;
                q.titleId = t.wii->channelTitleId;
                q.root = emuRoot(*t.wii->emuNand);
                return q;
            }
            if (key.size() < 4) return std::nullopt;
            q.kind = SaveKind::WiiDisc;
            q.id4 = std::string(key.substr(0, 4));
            q.root = t.wii->emuNand && !t.wii->emuNandFull ? emuRoot(*t.wii->emuNand) : std::string("nand:");
            return q;
        }
        case LaunchTarget::Kind::Channel:
            q.kind = SaveKind::Channel;
            q.titleId = t.titleId;
            q.root = "nand:";
            return q;
        case LaunchTarget::Kind::EmuChannel: return std::nullopt;  // erst nach applyEmuChannelPlan
        case LaunchTarget::Kind::Dol: break;
    }
    if (t.nintendont) {
        // Nintendont legt die emulierte Karte auf dem Spielgerät ab; von Disc gibt es kein Bibliotheksgerät.
        if (g.device == kNoDevice || libRoot.empty() || key.size() < 4) return std::nullopt;
        if (t.gc && t.gc->memcardReal) return std::nullopt;
        const bool multi = t.gc && (t.gc->nin.extraFlags & kNinCfgMcMulti) != 0;
        q.kind = multi ? SaveKind::GcMulti : SaveKind::GcSingle;
        q.id4 = std::string(key.substr(0, 4));
        q.jp = key[3] == 'J' || key[3] == 'K';
        q.root = std::string(libRoot);
        return q;
    }
    const EmulatorEntry* e = romEntry(g, t, emus);
    if (e == nullptr || g.path == nullptr) return std::nullopt;
    q.kind = SaveKind::Rom;
    q.console = g.console;
    q.profile = e->profile;
    q.saveDir = e->saveDir;
    q.saveExt = e->saveExt;
    q.stem = stemOf(g.path);
    q.gameKey = std::string(key);
    return q;
}

}  // namespace nl
