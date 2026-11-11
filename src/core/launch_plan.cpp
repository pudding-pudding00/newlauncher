#include "core/launch_plan.hpp"

#include <array>

#include "core/dol.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"

namespace nl {

namespace {

using PlanResult = Result<LaunchPlan, LaunchError>;
using TargetResult = Result<LaunchTarget, LaunchError>;

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

TargetResult gcTarget(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorEntry& nin, bool ahbprotOff) {
    auto err = [](LaunchFailure k, std::string c) { return TargetResult::err({k, std::move(c)}); };
    if (!ahbprotOff) return err(LaunchFailure::AhbProt, "");
    auto size = checkDol(fs, nin.dol, LaunchFailure::NinMissing);
    if (!size.isOk()) return TargetResult::err(size.error());
    const std::optional<NinVersion> ver = findNintendontVersion(fs, nin.dol);
    if (!ver) return err(LaunchFailure::NinUnknown, nin.dol);
    if (*ver < kMinNintendont) return err(LaunchFailure::NinTooOld, formatNinVersion(*ver));

    const std::string rel = g.path != nullptr ? g.path : "";
    const std::string game = joinPath(libRoot, rel);
    if (!exists(fs, game)) return err(LaunchFailure::RomMissing, game);
    const std::string gamePath = ninGamePath(rel);
    const bool usb = libRoot.substr(0, 4) == "usb:";
    auto cfg = buildNinCfg({gamePath, g.key != nullptr ? g.key : "", usb});
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
        case LaunchFailure::CiosMissing: return "CiosMissing";
        case LaunchFailure::CiosNotD2x: return "CiosNotD2x";
        case LaunchFailure::WiiFsUnsupported: return "WiiFsUnsupported";
        case LaunchFailure::WiiFileBroken: return "WiiFileBroken";
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
                                              const Result<FragList, LaunchError>& frags, bool dryRun) {
    using R = Result<WiiBootPlan, LaunchError>;
    if (!dryRun && cios.state == CiosState::Missing) return R::err({LaunchFailure::CiosMissing, ""});
    if (!dryRun && cios.state == CiosState::NotD2x) return R::err({LaunchFailure::CiosNotD2x, cios.version});
    if (!frags.isOk()) return R::err(frags.error());
    const std::string_view key = g.key != nullptr ? g.key : "";
    if (key.size() < 6) return R::err({LaunchFailure::Unsupported, consoleInfo(g.console).name});  // Scanner liefert immer ID6

    WiiBootPlan p;
    p.id6 = std::string(key.substr(0, 6));
    p.usb = libRoot.substr(0, 4) == "usb:";
    p.cios = cios;
    p.fragBytes = serializeFragList(frags.value());
    return R::ok(std::move(p));
}

Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus) {
    const EmulatorEntry* emu = emus.forConsole(g.console);
    if (emu == nullptr) return fail(LaunchFailure::Unsupported, consoleInfo(g.console).name);
    return planFor(fs, g, libRoot, *emu);
}

Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64, bool ahbprotOff) {
    if (g.console == Console::Gc) return gcTarget(fs, g, libRoot, emus.nintendont(), ahbprotOff);
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
