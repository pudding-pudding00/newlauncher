#include "core/launch_plan.hpp"

#include <array>

#include "core/dol.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"

namespace nl {

namespace {

using PlanResult = Result<LaunchPlan, LaunchError>;

PlanResult fail(LaunchFailure kind, std::string context) { return PlanResult::err({kind, std::move(context)}); }

// Größe der Datei über den Ordnereintrag; nullopt-artig über found=false.
bool fileSize(FileSystem& fs, std::string_view path, u64& size) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) return false;
    auto entries = fs.list(path.substr(0, slash));
    if (!entries.isOk()) return false;
    const std::string_view name = path.substr(slash + 1);
    for (const DirEntry& e : entries.value())
        if (!e.isDir && e.name == name) {
            size = e.size;
            return true;
        }
    return false;
}

}  // namespace

const char* launchFailureName(LaunchFailure f) {
    switch (f) {
        case LaunchFailure::Unsupported: return "Unsupported";
        case LaunchFailure::EmuMissing: return "EmuMissing";
        case LaunchFailure::EmuInvalid: return "EmuInvalid";
        case LaunchFailure::RomMissing: return "RomMissing";
        case LaunchFailure::ArgsTooLong: return "ArgsTooLong";
        case LaunchFailure::NoMemory: return "NoMemory";
    }
    return "?";
}

Result<LaunchPlan, LaunchError> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus) {
    const EmulatorEntry* emu = emus.forConsole(g.console);
    if (emu == nullptr) return fail(LaunchFailure::Unsupported, consoleInfo(g.console).name);

    u64 size = 0;
    if (!fileSize(fs, emu->dol, size)) return fail(LaunchFailure::EmuMissing, emu->dol);
    if (size > kMaxDolBytes) return fail(LaunchFailure::EmuInvalid, emu->dol);
    std::array<u8, kDolHeaderBytes> head{};
    auto got = fs.read(emu->dol, 0, head);
    if (!got.isOk()) return fail(LaunchFailure::EmuMissing, emu->dol);
    if (got.value() < kDolHeaderBytes || !parseDol(head, size).isOk()) return fail(LaunchFailure::EmuInvalid, emu->dol);

    const std::string rom = joinPath(libRoot, g.path != nullptr ? g.path : "");
    std::array<u8, 1> probe{};
    if (!fs.read(rom, 0, probe).isOk()) return fail(LaunchFailure::RomMissing, rom);

    LaunchPlan plan;
    plan.dolPath = emu->dol;
    plan.dolSize = static_cast<u32>(size);
    plan.args = expandArgs(emu->dol, emu->args, rom);
    auto block = packArgvBlock(plan.args, kArgsAddr);
    if (!block.isOk()) return fail(LaunchFailure::ArgsTooLong, block.error().context);
    plan.argvBlock = std::move(block).value();
    return PlanResult::ok(std::move(plan));
}

}  // namespace nl
