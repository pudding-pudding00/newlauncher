#include "core/launch_plan.hpp"

#include <array>

#include "core/dol.hpp"
#include "core/launch_args.hpp"
#include "core/launch_layout.hpp"

namespace nl {

namespace {

Result<LaunchPlan> fail(ErrorCode code, std::string context) { return Result<LaunchPlan>::err({code, std::move(context)}); }

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

Result<LaunchPlan> preflightEmulator(FileSystem& fs, const Game& g, std::string_view libRoot, const EmulatorTable& emus) {
    const EmulatorEntry* emu = emus.forConsole(g.console);
    if (emu == nullptr) return fail(ErrorCode::Unsupported, consoleInfo(g.console).name);

    u64 size = 0;
    if (!fileSize(fs, emu->dol, size)) return fail(ErrorCode::NotFound, emu->dol);
    if (size > kMaxDolBytes) return fail(ErrorCode::InvalidData, emu->dol);
    std::array<u8, kDolHeaderBytes> head{};
    auto got = fs.read(emu->dol, 0, head);
    if (!got.isOk()) return fail(ErrorCode::NotFound, emu->dol);
    if (got.value() < kDolHeaderBytes || !parseDol(head, size).isOk()) return fail(ErrorCode::InvalidData, emu->dol);

    const std::string rom = joinPath(libRoot, g.path != nullptr ? g.path : "");
    std::array<u8, 1> probe{};
    if (!fs.read(rom, 0, probe).isOk()) return fail(ErrorCode::NotFound, rom);

    LaunchPlan plan;
    plan.dolPath = emu->dol;
    plan.dolSize = static_cast<u32>(size);
    plan.args = expandArgs(emu->dol, emu->args, rom);
    auto block = packArgvBlock(plan.args, kArgsAddr);
    if (!block.isOk()) return fail(ErrorCode::InvalidData, block.error().context);
    plan.argvBlock = std::move(block).value();
    return Result<LaunchPlan>::ok(std::move(plan));
}

}  // namespace nl
