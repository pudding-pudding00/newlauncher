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

PlanResult planFor(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorEntry& emu) {
    const std::optional<u64> size = fileSize(fs, emu.dol);
    if (!size) return fail(LaunchFailure::EmuMissing, emu.dol);
    if (*size > kMaxDolBytes) return fail(LaunchFailure::EmuInvalid, emu.dol);
    std::array<u8, kDolHeaderBytes> head{};
    auto got = fs.read(emu.dol, 0, head);
    if (!got.isOk()) return fail(LaunchFailure::EmuMissing, emu.dol);
    if (got.value() < kDolHeaderBytes || !parseDol(head, *size).isOk()) return fail(LaunchFailure::EmuInvalid, emu.dol);

    const std::string rom = joinPath(libRoot, g.path != nullptr ? g.path : "");
    std::array<u8, 1> probe{};
    if (!fs.read(rom, 0, probe).isOk()) return fail(LaunchFailure::RomMissing, rom);

    LaunchPlan plan;
    plan.dolPath = emu.dol;
    plan.dolSize = static_cast<u32>(*size);
    plan.args = expandArgs(emu.dol, emu.args, rom);
    auto block = packArgvBlock(plan.args, kArgsAddr);
    if (!block.isOk()) return fail(LaunchFailure::ArgsTooLong, block.error().context);
    plan.argvBlock = std::move(block).value();
    return PlanResult::ok(std::move(plan));
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
    }
    return "?";
}

Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus) {
    const EmulatorEntry* emu = emus.forConsole(g.console);
    if (emu == nullptr) return fail(LaunchFailure::Unsupported, consoleInfo(g.console).name);
    return planFor(fs, g, libRoot, *emu);
}

Result<LaunchTarget, LaunchError> preflightGame(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus,
                                                const N64Inputs* n64) {
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
