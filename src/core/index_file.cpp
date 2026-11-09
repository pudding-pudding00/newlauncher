#include "core/index_file.hpp"

#include "core/bin_io.hpp"

namespace nl {

namespace {
constexpr u8 kMagic[4] = {'N', 'L', 'I', 'X'};
constexpr std::size_t kMinFolderBytes = 2 + 4;
constexpr std::size_t kMinGameBytes = 1 + 1 + 2 + 2 + 2 + 8;

Result<IndexData> invalid(const char* why) { return Result<IndexData>::err(Error{ErrorCode::InvalidData, why}); }
}  // namespace

std::vector<u8> writeIndex(const IndexData& data) {
    BeWriter w;
    for (u8 b : kMagic) w.u8(b);
    w.u16(kIndexVersion);
    w.u16(0);
    w.u32(static_cast<u32>(data.folders.size()));
    w.u32(static_cast<u32>(data.games.size()));
    for (const IndexFolder& f : data.folders) {
        w.str(f.path);
        w.u32(f.mtime);
    }
    for (const IndexGame& g : data.games) {
        w.u8(static_cast<u8>(g.console));
        w.u8(g.flags);
        w.str(g.key);
        w.str(g.title);
        w.str(g.path);
        w.u64(g.size);
    }
    return w.bytes();
}

Result<IndexData> readIndex(std::span<const u8> file) {
    BeReader r(file);
    u8 magic[4] = {};
    for (u8& b : magic) r.u8(b);
    u16 version = 0, reserved = 0;
    u32 folderCount = 0, gameCount = 0;
    r.u16(version);
    r.u16(reserved);
    r.u32(folderCount);
    r.u32(gameCount);
    if (!r.ok()) return invalid("index.bin: Header abgeschnitten");
    if (magic[0] != kMagic[0] || magic[1] != kMagic[1] || magic[2] != kMagic[2] || magic[3] != kMagic[3])
        return invalid("index.bin: falsches Magic");
    if (version != kIndexVersion) return invalid("index.bin: unbekannte Version");
    // Anzahlen gegen die Restgröße prüfen, bevor Speicher reserviert wird.
    if (folderCount > r.remaining() / kMinFolderBytes || gameCount > r.remaining() / kMinGameBytes)
        return invalid("index.bin: Anzahl passt nicht zur Dateigröße");

    IndexData d;
    d.folders.resize(folderCount);
    for (IndexFolder& f : d.folders) {
        r.str(f.path);
        r.u32(f.mtime);
    }
    d.games.resize(gameCount);
    for (IndexGame& g : d.games) {
        u8 console = 0;
        r.u8(console);
        r.u8(g.flags);
        r.str(g.key);
        r.str(g.title);
        r.str(g.path);
        r.u64(g.size);
        if (!r.ok()) break;
        if (console >= kConsoleCount) return invalid("index.bin: unbekannte Konsole");
        g.console = static_cast<Console>(console);
    }
    if (!r.ok()) return invalid("index.bin: abgeschnitten");
    if (r.remaining() != 0) return invalid("index.bin: Daten nach dem Ende");
    return Result<IndexData>::ok(std::move(d));
}

}  // namespace nl
