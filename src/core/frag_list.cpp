#include "core/frag_list.hpp"

#include "core/crc32.hpp"

#include <algorithm>

namespace nl {
namespace {

constexpr u64 kSector = 512;

// Hängt einen Eintrag an und verschmilzt ihn mit dem vorigen, wenn Offset und LBA lückenlos folgen.
bool append(FragList& l, FragEntry e) {
    if (!l.entries.empty()) {
        FragEntry& b = l.entries.back();
        if (b.offset + b.count == e.offset && b.sector + b.count == e.sector) {
            b.count += e.count;
            return true;
        }
    }
    if (l.entries.size() >= kMaxFrag) return false;
    l.entries.push_back(e);
    return true;
}

// Bildet die Dateisektoren [fileSector, fileSector + count) eines Teils über seine Extents ab.
// Ergebnis: OutOfFile, wenn der Bereich nicht vollständig in den Extents liegt.
Result<bool, FragError> mapRange(FragList& l, const FilePart& part, u32 discSector, u64 fileSector, u64 count) {
    using R = Result<bool, FragError>;
    while (count > 0) {
        // Extents sind nach fileSector sortiert: das letzte mit Beginn ≤ fileSector suchen.
        auto it = std::upper_bound(part.extents.begin(), part.extents.end(), fileSector,
                                   [](u64 s, const Extent& e) { return s < e.fileSector; });
        if (it == part.extents.begin()) return R::err(FragError::OutOfFile);
        const Extent& e = *--it;
        const u64 into = fileSector - e.fileSector;
        if (into >= e.count) return R::err(FragError::OutOfFile);
        const u64 n = std::min<u64>(count, e.count - into);
        if (!append(l, {discSector, static_cast<u32>(e.lba + into), static_cast<u32>(n)}))
            return R::err(FragError::TooFragmented);
        discSector += static_cast<u32>(n);
        fileSector += n;
        count -= n;
    }
    return R::ok(true);
}

void finish(FragList& l) {
    l.size = l.entries.empty() ? 0 : l.entries.back().offset + l.entries.back().count;
}

}  // namespace

Result<FragList, FragError> buildIsoFragList(const FilePart& iso) {
    using R = Result<FragList, FragError>;
    FragList l;
    const u64 sectors = (iso.size + kSector - 1) / kSector;
    auto r = mapRange(l, iso, 0, 0, sectors);
    if (!r.isOk()) return R::err(r.error());
    finish(l);
    return R::ok(std::move(l));
}

Result<FragList, FragError> buildWbfsFragList(const WbfsLayout& layout, std::span<const FilePart> parts) {
    using R = Result<FragList, FragError>;
    if (parts.empty()) return R::err(FragError::PartMissing);
    const bool split = parts.size() > 1;
    const u64 splitSize = parts[0].size;
    if (split && (splitSize == 0 || splitSize % kSector != 0)) return R::err(FragError::WbfsInvalid);

    FragList l;
    for (const DiscRun& run : discRuns(layout)) {
        u64 file = run.fileOffset;
        u64 disc = run.discOffset;
        u64 left = run.bytes;
        while (left > 0) {
            const u64 idx = split ? file / splitSize : 0;
            if (idx >= parts.size()) return R::err(FragError::PartMissing);
            const u64 local = file - idx * (split ? splitSize : 0);
            const FilePart& part = parts[idx];
            const u64 room = split && idx + 1 < parts.size() ? splitSize - local : left;  // bis zur Teilgrenze
            const u64 n = std::min(left, room);
            if (local + n > part.size) return R::err(FragError::OutOfFile);
            auto r = mapRange(l, part, static_cast<u32>(disc / kSector), local / kSector, n / kSector);
            if (!r.isOk()) return R::err(r.error());
            file += n;
            disc += n;
            left -= n;
        }
    }
    finish(l);
    return R::ok(std::move(l));
}

std::vector<u8> serializeFragList(const FragList& list) {
    std::vector<u8> b(kFragListBytes, 0);
    std::size_t at = 0;
    const auto put = [&](u32 v) {
        b[at++] = u8(v >> 24);
        b[at++] = u8(v >> 16);
        b[at++] = u8(v >> 8);
        b[at++] = u8(v);
    };
    put(list.size);
    put(static_cast<u32>(list.entries.size()));
    put(kMaxFrag);
    for (std::size_t i = 0; i < list.entries.size() && i < kMaxFrag; ++i) {
        put(list.entries[i].offset);
        put(list.entries[i].sector);
        put(list.entries[i].count);
    }
    return b;
}

u32 fragListCrc(std::span<const u8> bytes) { return crc32(bytes); }

}  // namespace nl
