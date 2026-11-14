#include "core/gc_disc.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "core/gc_fst.hpp"

namespace nl {

namespace {
constexpr u32 kCisoData = 0x8000;
constexpr std::size_t kCisoMapEntries = kCisoData - 8;
constexpr u32 kMaxBannerBytes = 0x10000;

u32 be32(const u8* b) { return static_cast<u32>(b[0]) << 24 | static_cast<u32>(b[1]) << 16 | static_cast<u32>(b[2]) << 8 | b[3]; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

Error invalid(const char* what) { return Error{ErrorCode::InvalidData, what}; }

// Liest Disc-Bytes aus einem ISO oder CISO. Bei CISO fehlen leere Blöcke in der Datei; sie lesen sich als Nullen.
class DiscReader {
public:
    DiscReader(FileSystem& fs, std::string path) : fs_(fs), path_(std::move(path)) {}

    Result<void> open() {
        u8 head[8];
        auto n = fs_.read(path_, 0, head);
        if (!n.isOk()) return Result<void>::err(n.error());
        if (n.value() < 8 || std::memcmp(head, "CISO", 4) != 0) return Result<void>::ok();
        block_ = static_cast<u32>(head[4]) | static_cast<u32>(head[5]) << 8 | static_cast<u32>(head[6]) << 16 |
                 static_cast<u32>(head[7]) << 24;
        if (block_ == 0 || block_ > 16u * 1024 * 1024) return Result<void>::err(invalid("CISO: Blockgröße"));
        std::vector<u8> map(kCisoMapEntries);
        auto m = fs_.read(path_, 8, map);
        if (!m.isOk()) return Result<void>::err(m.error());
        if (m.value() != map.size()) return Result<void>::err(invalid("CISO: Blockkarte"));
        rank_.assign(kCisoMapEntries, -1);
        s32 next = 0;
        for (std::size_t b = 0; b < kCisoMapEntries; ++b)
            if (map[b] != 0) rank_[b] = next++;
        return Result<void>::ok();
    }

    Result<void> read(u64 offset, std::span<u8> out) {
        if (rank_.empty()) return exact(offset, out);
        while (!out.empty()) {
            const u64 b = offset / block_;
            const u32 in = static_cast<u32>(offset % block_);
            const std::size_t n = std::min<std::size_t>(out.size(), block_ - in);
            if (b >= rank_.size()) return Result<void>::err(invalid("CISO: hinter der Blockkarte"));
            if (rank_[b] < 0) {
                std::memset(out.data(), 0, n);
            } else if (auto r = exact(kCisoData + u64{static_cast<u32>(rank_[b])} * block_ + in, out.first(n)); !r.isOk()) {
                return r;
            }
            offset += n;
            out = out.subspan(n);
        }
        return Result<void>::ok();
    }

private:
    Result<void> exact(u64 offset, std::span<u8> out) {
        auto n = fs_.read(path_, offset, out);
        if (!n.isOk()) return Result<void>::err(n.error());
        if (n.value() != out.size()) return Result<void>::err(invalid("Disc: hinter dem Dateiende"));
        return Result<void>::ok();
    }

    FileSystem& fs_;
    std::string path_;
    u32 block_ = 0;
    std::vector<s32> rank_;  // CISO: Index des Blocks in der Datei, -1 = leer; leer = ISO
};

Result<std::vector<u8>> bannerFromImage(FileSystem& fs, std::string_view path) {
    using R = Result<std::vector<u8>>;
    DiscReader disc(fs, std::string(path));
    if (auto r = disc.open(); !r.isOk()) return R::err(r.error());
    u8 head[8];
    if (auto r = disc.read(0x424, head); !r.isOk()) return R::err(r.error());
    const u32 fstOffset = be32(head), fstSize = be32(head + 4);
    if (fstSize < 12 || fstSize > kGcFstMaxBytes) return R::err(invalid("FST-Größe"));
    std::vector<u8> fst(fstSize);
    if (auto r = disc.read(fstOffset, fst); !r.isOk()) return R::err(r.error());
    auto file = findRootFile(fst, "opening.bnr");
    if (!file.isOk()) return R::err(file.error());
    if (file.value().size > kMaxBannerBytes) return R::err(invalid("opening.bnr zu groß"));
    std::vector<u8> bnr(file.value().size);
    if (auto r = disc.read(file.value().offset, bnr); !r.isOk()) return R::err(r.error());
    return R::ok(std::move(bnr));
}

// Entpacktes Abbild: Dateien unter root/ (Nintendont) oder files/ (Dolphin).
Result<std::vector<u8>> bannerFromFolder(FileSystem& fs, std::string_view dir, const std::vector<DirEntry>& entries) {
    for (const DirEntry& d : entries) {
        if (!d.isDir) continue;
        const std::string name = lower(d.name);
        if (name != "root" && name != "files") continue;
        const std::string sub = joinPath(dir, d.name);
        auto files = fs.list(sub);
        if (!files.isOk()) continue;
        for (const DirEntry& f : files.value())
            if (!f.isDir && lower(f.name) == "opening.bnr") return fs.readAll(joinPath(sub, f.name), kMaxBannerBytes);
    }
    return Result<std::vector<u8>>::err(Error{ErrorCode::NotFound, "opening.bnr"});
}
}  // namespace

Result<GcBanner> loadGcBanner(FileSystem& fs, std::string_view gamePath, u8 langIndex, bool shiftJis) {
    Result<std::vector<u8>> bnr = Result<std::vector<u8>>::err(Error{ErrorCode::NotFound, ""});
    auto entries = fs.list(gamePath);
    bool folder = false;
    if (entries.isOk())
        for (const DirEntry& e : entries.value()) folder = folder || (e.isDir && lower(e.name) == "sys");
    bnr = folder ? bannerFromFolder(fs, gamePath, entries.value()) : bannerFromImage(fs, gamePath);
    if (!bnr.isOk()) return Result<GcBanner>::err(bnr.error());
    return parseGcBanner(bnr.value(), langIndex, shiftJis);
}

}  // namespace nl
