#include "core/launch_plan.hpp"

#include <algorithm>
#include <array>

#include "core/dol.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"
#include "core/library_merge.hpp"

namespace nl {

namespace {

using PlanResult = Result<LaunchPlan, LaunchError>;
using TargetResult = Result<LaunchTarget, LaunchError>;

// Wurzel liegt auf einem USB-Gerät („usb:“, „usb2:“ …).
bool onUsb(std::string_view libRoot) { return libRoot.substr(0, 3) == "usb"; }

// Weitere USB-Partition (usb2 … usb8): nur der Game-Booter liest dort selbst (Fragmentliste mit LBAs).
// Kontext für den Dialog: Gerätename wie in der Detailansicht („USB 2“).
std::optional<std::string> secondaryUsb(std::string_view libRoot) {
    if (!onUsb(libRoot) || libRoot.size() < 4 || libRoot[3] == ':') return std::nullopt;
    const std::size_t colon = libRoot.find(':');
    return deviceLabel(libRoot.substr(0, colon));
}

PlanResult fail(LaunchFailure kind, std::string context) { return PlanResult::err({kind, std::move(context)}); }

// Prüft die DOL; liefert ihre Größe. missing ist die Fehlerart, wenn sie fehlt.
Result<u32, LaunchError> checkDol(FileSystem& fs, const std::string& dol, LaunchFailure missing) {
    using R = Result<u32, LaunchError>;
    const std::optional<u64> size = fileSize(fs, dol);
    if (!size) return R::err({missing, dol});
    if (*size > kMaxDolBytes) return R::err({LaunchFailure::EmuInvalid, dol});
    std::array<u8, kDolHeaderBytes> head{};
    auto got = fs.read(dol, 0, head);
    if (!got.isOk()) return R::err({missing, dol});
    if (got.value() < kDolHeaderBytes || !parseDol(head, *size).isOk()) return R::err({LaunchFailure::EmuInvalid, dol});
    return R::ok(static_cast<u32>(*size));
}

bool exists(FileSystem& fs, const std::string& file) {
    std::array<u8, 1> probe{};
    return fs.read(file, 0, probe).isOk();
}

PlanResult finish(LaunchPlan plan) {
    auto block = packArgvBlock(plan.args, kArgsAddr);
    if (!block.isOk()) return fail(LaunchFailure::ArgsTooLong, block.error().context);
    plan.argvBlock = std::move(block).value();
    return PlanResult::ok(std::move(plan));
}

PlanResult planFor(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorEntry& emu) {
    if (auto dev = secondaryUsb(libRoot)) return fail(LaunchFailure::DeviceUnsupported, std::move(*dev));
    auto size = checkDol(fs, emu.dol, LaunchFailure::EmuMissing);
    if (!size.isOk()) return PlanResult::err(size.error());

    const std::string rom = joinPath(libRoot, g.path != nullptr ? g.path : "");
    if (!exists(fs, rom)) return fail(LaunchFailure::RomMissing, rom);

    LaunchPlan plan;
    plan.dolPath = emu.dol;
    plan.dolSize = size.value();
    plan.args = expandArgs(emu.dol, emu.args, rom);
    return finish(std::move(plan));
}

TargetResult gcTarget(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorEntry& nin, bool ahbprotOff,
                      const GameOptionInputs* opts) {
    auto err = [](LaunchFailure k, std::string c) { return TargetResult::err({k, std::move(c)}); };
    if (auto dev = secondaryUsb(libRoot)) return err(LaunchFailure::DeviceUnsupported, std::move(*dev));
    if (!ahbprotOff) return err(LaunchFailure::AhbProt, "");
    auto size = checkDol(fs, nin.dol, LaunchFailure::NinMissing);
    if (!size.isOk()) return TargetResult::err(size.error());
    const std::optional<NinVersion> ver = findNintendontVersion(fs, nin.dol);
    if (!ver) return err(LaunchFailure::NinUnknown, nin.dol);
    if (*ver < kMinNintendont) return err(LaunchFailure::NinTooOld, formatNinVersion(*ver));

    const std::string rel = g.path != nullptr ? g.path : "";
    const std::string game = joinPath(libRoot, rel);
    // Entpacktes Abbild (FST): Nintendont hängt „sys/boot.bin“ direkt an den Pfad, er braucht also „/“ am Ende.
    const bool fst = exists(fs, joinPath(game, "sys/boot.bin"));
    if (!fst && !exists(fs, game)) return err(LaunchFailure::RomMissing, game);
    if (!fst) {
        // Nintendont (IsSupportedFileExt, Stand c683db3) prüft nur die letzte Endung.
        const std::size_t dot = rel.rfind('.');
        const std::size_t slash = rel.rfind('/');
        const std::string ext = dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "" : rel.substr(dot);
        std::string low = ext;
        for (char& ch : low)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        if (low != ".iso" && low != ".gcm" && low != ".cso" && low != ".ciso") return err(LaunchFailure::NinFormat, ext);
    }
    const std::string gamePath = fst ? ninGamePath(rel) + "/" : ninGamePath(rel);
    const bool usb = onUsb(libRoot);
    const std::string_view key = g.key != nullptr ? g.key : "";
    std::optional<GcOptions> gc;
    std::vector<LaunchWarning> warnings;
    NinConfigInput in{gamePath, key, usb};
    std::string cheatPath;  // muss bis buildNinCfg leben (in.cheatPath ist eine Sicht)
    if (opts != nullptr) {
        gc = resolveGcOptions(opts->config, opts->games, key.substr(0, std::min<std::size_t>(6, key.size())), *ver);
        in = gc->nin;
        in.gamePath = gamePath;
        in.id6 = key;
        in.usb = usb;
        if (!opts->gcPorts && gc->memcardReal) warnings.push_back(LaunchWarning::MemcardRealNoPorts);
        if (!opts->gcPorts && gc->nativeSi) warnings.push_back(LaunchWarning::NativeSiNoPorts);
        if (gc->cheats && !opts->gcCheats.empty()) {
            if (opts->gcCheats.size() > kNinCheatBytes) return err(LaunchFailure::CheatsTooLarge, std::to_string(opts->gcCheats.size()));
            cheatPath = ninCheatPath(key.substr(0, std::min<std::size_t>(6, key.size())));
            in.cheatPath = cheatPath;
        }
    }
    auto cfg = buildNinCfg(in);
    if (!cfg.isOk()) {
        if (cfg.error() == NinCfgError::PathTooLong) return err(LaunchFailure::NinPathTooLong, gamePath);
        return err(LaunchFailure::Unsupported, consoleInfo(g.console).name);  // Schlüssel ohne ID6 (Scanner liefert immer ID6)
    }

    LaunchPlan plan;
    plan.dolPath = nin.dol;
    plan.dolSize = size.value();
    plan.args = {nin.dol, std::string(cfg.value().begin(), cfg.value().end())};
    PlanResult done = finish(std::move(plan));
    if (!done.isOk()) return TargetResult::err(done.error());
    LaunchTarget t;
    t.dol = std::move(done).value();
    t.nintendont = ver;
    t.gc = std::move(gc);
    t.warnings = std::move(warnings);
    if (!cheatPath.empty()) {
        // Nintendont mountet nur das Spielgerät: die GCT liegt dort, nicht im Datenordner (Spec v2 8).
        t.gct.assign(opts->gcCheats.begin(), opts->gcCheats.end());
        t.gctPath = joinPath(libRoot.substr(0, libRoot.find(':') + 1), cheatPath.substr(1));
    }
    return TargetResult::ok(std::move(t));
}
TargetResult dolTarget(PlanResult plan, std::optional<N64Method> n64) {
    if (!plan.isOk()) return TargetResult::err(plan.error());
    LaunchTarget t;
    t.dol = std::move(plan).value();
    t.n64 = n64;
    return TargetResult::ok(std::move(t));
}

}  // namespace

std::optional<u64> fileSize(FileSystem& fs, std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) return std::nullopt;
    auto entries = fs.list(path.substr(0, slash));
    if (!entries.isOk()) return std::nullopt;
    const std::string_view name = path.substr(slash + 1);
    for (const DirEntry& e : entries.value())
        if (!e.isDir && e.name == name) return e.size;
    return std::nullopt;
}

const char* launchWarningName(LaunchWarning w) {
    switch (w) {
        case LaunchWarning::MemcardRealNoPorts: return "memcard_real_no_ports";
        case LaunchWarning::NativeSiNoPorts: return "native_si_no_ports";
    }
    return "?";
}

const char* launchFailureName(LaunchFailure f) {
    switch (f) {
        case LaunchFailure::Unsupported: return "Unsupported";
        case LaunchFailure::EmuMissing: return "EmuMissing";
        case LaunchFailure::EmuInvalid: return "EmuInvalid";
        case LaunchFailure::RomMissing: return "RomMissing";
        case LaunchFailure::ArgsTooLong: return "ArgsTooLong";
        case LaunchFailure::NoMemory: return "NoMemory";
        case LaunchFailure::NoN64Path: return "NoN64Path";
        case LaunchFailure::NinMissing: return "NinMissing";
        case LaunchFailure::NinUnknown: return "NinUnknown";
        case LaunchFailure::NinTooOld: return "NinTooOld";
        case LaunchFailure::NinPathTooLong: return "NinPathTooLong";
        case LaunchFailure::AhbProt: return "AhbProt";
        case LaunchFailure::NinFormat: return "NinFormat";
        case LaunchFailure::CiosMissing: return "CiosMissing";
        case LaunchFailure::WiiFsUnsupported: return "WiiFsUnsupported";
        case LaunchFailure::WiiFileBroken: return "WiiFileBroken";
        case LaunchFailure::DeviceUnsupported: return "DeviceUnsupported";
        case LaunchFailure::CheatsTooLarge: return "CheatsTooLarge";
    }
    return "?";
}

LaunchError fromFatError(FatError e, std::string context) {
    switch (e) {
        case FatError::SectorSize:
        case FatError::NoFatVolume:
        case FatError::NotFat32: return {LaunchFailure::WiiFsUnsupported, std::move(context)};
        case FatError::Io:
        case FatError::BadChain:
        case FatError::ChainTooShort: break;
    }
    return {LaunchFailure::WiiFileBroken, std::move(context)};
}

LaunchError fromFragError(FragError e, std::string context) {
    if (e == FragError::PartMissing) return {LaunchFailure::RomMissing, std::move(context)};
    return {LaunchFailure::WiiFileBroken, std::move(context)};
}

Result<WiiBootPlan, LaunchError> preflightWii(const Game& g, std::string_view libRoot, const CiosStatus& cios,
                                              const Result<FragList, LaunchError>& frags, bool dryRun,
                                              const WiiOptions& options, std::span<const u8> gct,
                                              std::span<const WipEntry> wip) {
    using R = Result<WiiBootPlan, LaunchError>;
    if (!dryRun && cios.state == CiosState::Missing) {
        return R::err({LaunchFailure::CiosMissing, std::to_string(static_cast<unsigned>(cios.slot))});
    }
    if (!frags.isOk()) return R::err(frags.error());
    const std::string_view key = g.key != nullptr ? g.key : "";
    if (key.size() < 6) return R::err({LaunchFailure::Unsupported, consoleInfo(g.console).name});  // Scanner liefert immer ID6
    if (gct.size() > kWiiCodelistBytes) return R::err({LaunchFailure::CheatsTooLarge, std::to_string(gct.size())});

    WiiBootPlan p;
    p.id6 = std::string(key.substr(0, 6));
    p.usb = onUsb(libRoot);
    p.cios = cios;
    p.iosSlot = cios.slot;
    p.fragBytes = serializeFragList(frags.value());
    p.options = options;
    p.gct.assign(gct.begin(), gct.end());
    p.hookType = gct.empty() ? 0 : options.hookType;  // Spec v2 8: ohne Codes kein Hook
    p.wip.assign(wip.begin(), wip.begin() + static_cast<std::ptrdiff_t>(std::min(wip.size(), kWipMaxEntries)));
    if (cios.state != CiosState::D2x) p.options.cfg.returnTo = 0;  // Spec 7.2: Rückkehr nur mit d2x
    return R::ok(std::move(p));
}

Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus) {
    const EmulatorEntry* emu = emus.forConsole(g.console);
    if (emu == nullptr) return fail(LaunchFailure::Unsupported, consoleInfo(g.console).name);
    return planFor(fs, g, libRoot, *emu);
}

Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64, bool ahbprotOff, const GameOptionInputs* opts) {
    if (g.console == Console::Gc) return gcTarget(fs, g, libRoot, emus.nintendont(), ahbprotOff, opts);
    if (g.console == Console::Wii) {
        LaunchTarget t;
        t.kind = LaunchTarget::Kind::Wii;
        return TargetResult::ok(std::move(t));
    }
    if (g.console != Console::N64) return dolTarget(preflightEmulator(fs, g, libRoot, emus), std::nullopt);

    const N64Decision d = n64 != nullptr ? decideN64(*n64) : N64Decision{};
    if (!d.chosen) return TargetResult::err({LaunchFailure::NoN64Path, consoleInfo(g.console).name});
    if (*d.chosen == N64Method::Vc) {
        LaunchTarget t;
        t.kind = LaunchTarget::Kind::Channel;
        t.titleId = *n64->mappedTitle;
        t.n64 = N64Method::Vc;
        return TargetResult::ok(std::move(t));
    }
    return dolTarget(planFor(fs, g, libRoot, *emus.forN64(*d.chosen)), d.chosen);
}

}  // namespace nl
