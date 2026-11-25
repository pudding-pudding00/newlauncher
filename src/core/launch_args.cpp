#include "core/launch_args.hpp"

#include "core/bin_io.hpp"

namespace nl {

namespace {

void replaceAll(std::string& s, std::string_view from, std::string_view to) {
    for (std::size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

}  // namespace

std::vector<std::string> expandArgs(std::string_view dolPath, std::string_view schema, std::string_view romPath) {
    const std::size_t slash = romPath.rfind('/');
    const std::string_view dir = slash == std::string_view::npos ? std::string_view{} : romPath.substr(0, slash + 1);
    const std::string_view file = slash == std::string_view::npos ? romPath : romPath.substr(slash + 1);

    std::vector<std::string> out{std::string(dolPath)};
    if (schema.empty()) return out;
    std::size_t start = 0;
    while (true) {
        const std::size_t bar = schema.find('|', start);
        std::string arg(schema.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
        replaceAll(arg, "{dir}", dir);
        replaceAll(arg, "{file}", file);
        replaceAll(arg, "{path}", romPath);
        out.push_back(std::move(arg));
        if (bar == std::string_view::npos) break;
        start = bar + 1;
    }
    return out;
}

Result<std::vector<u8>> packArgvBlock(std::span<const std::string> args, u32 baseAddr) {
    std::size_t cmdBytes = 1;  // abschließendes zweites NUL
    for (const std::string& a : args) cmdBytes += a.size() + 1;
    if (kArgvHeaderBytes + cmdBytes > kMaxArgvBytes)
        return Result<std::vector<u8>>::err({ErrorCode::InvalidData, "Argumente zu lang"});

    BeWriter w;
    w.u32(kArgvMagic);
    w.u32(baseAddr + kArgvHeaderBytes);
    w.u32(static_cast<u32>(cmdBytes - 1));
    w.u32(0);  // argc – füllt __CheckARGV in der Ziel-DOL
    w.u32(0);  // argv
    w.u32(0);  // endARGV
    for (const std::string& a : args) {
        for (char c : a) w.u8(static_cast<u8>(c));
        w.u8(0);
    }
    w.u8(0);
    return Result<std::vector<u8>>::ok(w.bytes());
}

bool hasArgFlag(int argc, const char* const* argv, std::string_view flag) {
    if (argv == nullptr) return false;
    for (int i = 0; i < argc; ++i)
        if (argv[i] != nullptr && std::string_view(argv[i]) == flag) return true;
    return false;
}

}  // namespace nl
