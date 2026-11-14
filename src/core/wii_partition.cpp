#include "core/wii_partition.hpp"

#include <algorithm>
#include <cstring>

#include "core/gc_fst.hpp"
#include "core/wbfs_map.hpp"

namespace nl {

namespace {

constexpr u64 kPartTable = 0x40000;
constexpr u32 kPartGroups = 4;
constexpr u32 kMaxPartsPerGroup = 16;  // Discs haben 1–3; mehr ist Unsinn
constexpr u32 kTicketKey = 0x1BF;
constexpr u32 kTicketTitleId = 0x1DC;
constexpr u32 kTicketKeyIndex = 0x1F1;
constexpr u32 kDataOffset = 0x2B8;
constexpr u32 kCluster = 0x8000;
constexpr u32 kHashArea = 0x400;
constexpr u32 kClusterData = kCluster - kHashArea;  // 0x7C00
constexpr u32 kClusterIv = 0x3D0;

u32 be32(const u8* p) {
    return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}

Error invalid(const char* what) { return Error{ErrorCode::InvalidData, what}; }

template <class T>
Result<T> fail(Error e) {
    return Result<T>::err(std::move(e));
}

// Erste Partition vom Typ 0 (Spiel) aus allen vier Tabellen-Gruppen.
Result<u64> findGamePartition(DiscSource& disc) {
    u8 groups[kPartGroups * 8];
    if (auto r = disc.read(kPartTable, groups); !r.isOk()) return fail<u64>(r.error());
    for (u32 g = 0; g < kPartGroups; ++g) {
        const u32 count = be32(groups + g * 8);
        const u64 table = u64{be32(groups + g * 8 + 4)} << 2;
        if (count == 0) continue;
        if (count > kMaxPartsPerGroup) return fail<u64>(invalid("Wii: Partitionstabelle"));
        std::vector<u8> entries(std::size_t{count} * 8);
        if (auto r = disc.read(table, entries); !r.isOk()) return fail<u64>(r.error());
        for (u32 i = 0; i < count; ++i)
            if (be32(entries.data() + i * 8 + 4) == 0) return Result<u64>::ok(u64{be32(entries.data() + i * 8)} << 2);
    }
    return fail<u64>(Error{ErrorCode::NotFound, "Wii: keine Spielpartition"});
}

// Liest entschlüsselte Partitionsdaten; hält den zuletzt entschlüsselten Cluster.
class PartitionReader {
public:
    PartitionReader(DiscSource& disc, u64 dataStart, u64 dataSize, const AesBlock& titleKey)
        : disc_(disc), start_(dataStart), clusters_(dataSize / kCluster), aes_(titleKey) {}

    Result<void> read(u64 offset, std::span<u8> out) {
        while (!out.empty()) {
            const u64 c = offset / kClusterData;
            const u32 in = static_cast<u32>(offset % kClusterData);
            const std::size_t n = std::min<std::size_t>(out.size(), kClusterData - in);
            if (auto r = load(c); !r.isOk()) return r;
            std::memcpy(out.data(), plain_.data() + in, n);
            offset += n;
            out = out.subspan(n);
        }
        return Result<void>::ok();
    }

private:
    Result<void> load(u64 c) {
        if (have_ && c == cached_) return Result<void>::ok();
        if (c >= clusters_) return Result<void>::err(invalid("Wii: hinter den Partitionsdaten"));
        if (auto r = disc_.read(start_ + c * kCluster, raw_); !r.isOk()) return r;
        AesBlock iv{};
        std::memcpy(iv.data(), raw_.data() + kClusterIv, iv.size());
        aesCbcDecrypt(aes_, iv, std::span<const u8>(raw_).subspan(kHashArea), plain_);
        have_ = true;
        cached_ = c;
        return Result<void>::ok();
    }

    DiscSource& disc_;
    u64 start_;
    u64 clusters_;
    Aes128 aes_;
    std::vector<u8> raw_ = std::vector<u8>(kCluster);
    std::vector<u8> plain_ = std::vector<u8>(kClusterData);
    bool have_ = false;
    u64 cached_ = 0;
};

class FileSource : public DiscSource {
public:
    FileSource(FileSystem& fs, std::string path) : fs_(fs), path_(std::move(path)) {}
    Result<void> read(u64 offset, std::span<u8> out) override { return exact(offset, out); }

protected:
    Result<void> exact(u64 offset, std::span<u8> out) {
        auto n = fs_.read(path_, offset, out);
        if (!n.isOk()) return Result<void>::err(n.error());
        if (n.value() != out.size()) return Result<void>::err(invalid("Disc: hinter dem Dateiende"));
        return Result<void>::ok();
    }

private:
    FileSystem& fs_;
    std::string path_;
};

class WbfsSource final : public FileSource {
public:
    WbfsSource(FileSystem& fs, std::string path, WbfsLayout layout) : FileSource(fs, std::move(path)), l_(std::move(layout)) {}

    Result<void> read(u64 offset, std::span<u8> out) override {
        const u64 s = l_.wbfsSecSize;
        while (!out.empty()) {
            const u64 b = offset / s;
            const u64 in = offset % s;
            const std::size_t n = static_cast<std::size_t>(std::min<u64>(out.size(), s - in));
            if (b >= l_.wlba.size()) return Result<void>::err(invalid("WBFS: hinter der Disc"));
            if (l_.wlba[b] == 0) {
                std::memset(out.data(), 0, n);
            } else if (auto r = exact(u64{l_.wlba[b]} * s + in, out.first(n)); !r.isOk()) {
                return r;
            }
            offset += n;
            out = out.subspan(n);
        }
        return Result<void>::ok();
    }

private:
    WbfsLayout l_;
};

bool endsWithNoCase(const std::string& s, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

}  // namespace

Result<std::unique_ptr<DiscSource>> openWiiDiscFile(FileSystem& fs, std::string path) {
    using R = Result<std::unique_ptr<DiscSource>>;
    if (endsWithNoCase(path, ".iso")) return R::ok(std::make_unique<FileSource>(fs, std::move(path)));
    if (!endsWithNoCase(path, ".wbfs")) return R::err(Error{ErrorCode::Unsupported, "Wii: Dateityp"});
    // Geteilte Dateien (.wbf1 …): Disc-Blöcke über mehrere Dateien – vorerst Cover (Ruling M18 T9).
    const std::string part1 = path.substr(0, path.size() - 1) + "1";
    u8 probe[1];
    if (auto p = fs.read(part1, 0, probe); p.isOk()) return R::err(Error{ErrorCode::Unsupported, "WBFS geteilt"});
    u8 first[512];
    auto got = fs.read(path, 0, first);
    if (!got.isOk()) return R::err(got.error());
    if (got.value() < sizeof first) return R::err(invalid("WBFS: Kopf"));
    const u8 shift = first[9];
    if (shift < 15 || shift > 30) return R::err(invalid("WBFS: Blockgröße"));
    std::vector<u8> head(wbfsHeadBytes(shift));
    auto all = fs.read(path, 0, head);
    if (!all.isOk()) return R::err(all.error());
    if (all.value() != head.size()) return R::err(invalid("WBFS: Kopf"));
    auto layout = parseWbfsHead(head);
    if (!layout.isOk()) return R::err(invalid("WBFS: Kopf"));
    return R::ok(std::make_unique<WbfsSource>(fs, std::move(path), std::move(layout.value())));
}

Result<std::vector<u8>> readWiiOpeningBnr(DiscSource& disc, const TitleKeyDecrypt& decrypt, u32 maxBytes) {
    using R = Result<std::vector<u8>>;
    auto part = findGamePartition(disc);
    if (!part.isOk()) return R::err(part.error());
    const u64 p = part.value();

    u8 ticket[0x200];
    if (auto r = disc.read(p, ticket); !r.isOk()) return R::err(r.error());
    WiiTicketKey key{};
    std::memcpy(key.encrypted.data(), ticket + kTicketKey, 16);
    std::memcpy(key.iv.data(), ticket + kTicketTitleId, 8);
    key.index = ticket[kTicketKeyIndex];
    if (key.index > 1) return R::err(invalid("Wii: Schlüsselindex"));

    u8 data[8];
    if (auto r = disc.read(p + kDataOffset, data); !r.isOk()) return R::err(r.error());
    const u64 dataStart = p + (u64{be32(data)} << 2);
    const u64 dataSize = u64{be32(data + 4)} << 2;

    const std::optional<AesBlock> titleKey = decrypt(key);
    if (!titleKey) return R::err(Error{ErrorCode::IoError, "es"});
    PartitionReader reader(disc, dataStart, dataSize, *titleKey);

    u8 header[0x430];
    if (auto r = reader.read(0, header); !r.isOk()) return R::err(r.error());
    const u64 fstOffset = u64{be32(header + 0x424)} << 2;
    const u64 fstSize = u64{be32(header + 0x428)} << 2;
    if (fstSize < 12 || fstSize > kWiiFstMax) return R::err(invalid("Wii: FST-Größe"));
    std::vector<u8> fst(static_cast<std::size_t>(fstSize));
    if (auto r = reader.read(fstOffset, fst); !r.isOk()) return R::err(r.error());
    auto file = findRootFile(fst, "opening.bnr");
    if (!file.isOk()) return R::err(file.error());
    if (file.value().size > maxBytes) return R::err(invalid("opening.bnr zu groß"));
    std::vector<u8> bnr(file.value().size);
    if (auto r = reader.read(u64{file.value().offset} << 2, bnr); !r.isOk()) return R::err(r.error());
    return R::ok(std::move(bnr));
}

}  // namespace nl
