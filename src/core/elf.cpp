#include "core/elf.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "core/bin_io.hpp"
#include "core/dol.hpp"

namespace nl {
namespace {

constexpr u8 kElfClass32 = 1;
constexpr u8 kElfData2Msb = 2;
constexpr u8 kElfVersionCurrent = 1;
constexpr u16 kEtExec = 2;
constexpr u16 kEmPpc = 20;
constexpr u32 kPtLoad = 1;
constexpr u32 kPfX = 1;
constexpr std::size_t kElfHeaderBytes = 52;
constexpr std::size_t kElfPhdrBytes = 32;

u32 normalizeWiiAddr(u32 addr) {
    if (addr < 0x18000000u) {
        return addr | 0x80000000u;
    }
    return addr;
}

struct LoadSeg {
    u32 fileOffset = 0;
    u32 vaddr = 0;
    u32 fileSize = 0;
    u32 memSize = 0;
    bool executable = false;
};

void putBe32(std::vector<u8>& buf, std::size_t off, u32 v) {
    buf[off + 0] = static_cast<u8>(v >> 24);
    buf[off + 1] = static_cast<u8>(v >> 16);
    buf[off + 2] = static_cast<u8>(v >> 8);
    buf[off + 3] = static_cast<u8>(v);
}

}  // namespace

bool isElfMagic(std::span<const u8> data) {
    return data.size() >= 4 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

Result<std::vector<u8>> elfToDol(std::span<const u8> elf, u32 maxDolBytes) {
    using R = Result<std::vector<u8>>;
    if (elf.size() < kElfHeaderBytes || !isElfMagic(elf)) {
        return R::err({ErrorCode::InvalidData, "elf: invalid header or magic"});
    }
    if (elf[4] != kElfClass32) {
        return R::err({ErrorCode::InvalidData, "elf: must be 32-bit"});
    }
    if (elf[5] != kElfData2Msb) {
        return R::err({ErrorCode::InvalidData, "elf: must be big-endian"});
    }
    if (elf[6] != kElfVersionCurrent) {
        return R::err({ErrorCode::InvalidData, "elf: invalid ident version"});
    }

    BeReader r(elf.subspan(16, kElfHeaderBytes - 16));
    u16 type = 0, machine = 0, ehsize = 0, phentsize = 0, phnum = 0;
    u32 version = 0, entry = 0, phoff = 0, shoff = 0, flags = 0;
    r.u16(type);
    r.u16(machine);
    r.u32(version);
    r.u32(entry);
    r.u32(phoff);
    r.u32(shoff);
    r.u32(flags);
    r.u16(ehsize);
    r.u16(phentsize);
    r.u16(phnum);
    if (!r.ok() || type != kEtExec || machine != kEmPpc || version != kElfVersionCurrent) {
        return R::err({ErrorCode::InvalidData, "elf: must be PowerPC ET_EXEC"});
    }
    if (phnum == 0 || phentsize < kElfPhdrBytes) {
        return R::err({ErrorCode::InvalidData, "elf: missing program headers"});
    }
    const u64 phTableEnd = u64{phoff} + u64{phnum} * u64{phentsize};
    if (phTableEnd > elf.size()) {
        return R::err({ErrorCode::InvalidData, "elf: program headers truncated"});
    }

    std::vector<LoadSeg> textSegs;
    std::vector<LoadSeg> dataSegs;
    std::vector<std::pair<u32, u32>> memRanges;
    u32 bssBegin = 0;
    u32 bssEnd = 0;

    for (u16 i = 0; i < phnum; ++i) {
        const std::size_t off = static_cast<std::size_t>(phoff) + static_cast<std::size_t>(i) * phentsize;
        BeReader pr(elf.subspan(off, kElfPhdrBytes));
        u32 pType = 0, pOffset = 0, pVaddr = 0, pPaddr = 0, pFilesz = 0, pMemsz = 0, pFlags = 0, pAlign = 0;
        pr.u32(pType);
        pr.u32(pOffset);
        pr.u32(pVaddr);
        pr.u32(pPaddr);
        pr.u32(pFilesz);
        pr.u32(pMemsz);
        pr.u32(pFlags);
        pr.u32(pAlign);
        if (pType != kPtLoad || pMemsz == 0) continue;
        if (pFilesz > pMemsz) {
            return R::err({ErrorCode::InvalidData, "elf: p_filesz > p_memsz"});
        }
        if (u64{pOffset} + u64{pFilesz} > elf.size()) {
            return R::err({ErrorCode::InvalidData, "elf: segment truncated"});
        }
        const u32 addr = normalizeWiiAddr(pVaddr);
        if (u64{addr} + u64{pMemsz} > 0xFFFFFFFFu) {
            return R::err({ErrorCode::InvalidData, "elf: segment address overflow"});
        }
        memRanges.emplace_back(addr, addr + pMemsz);

        if (pFilesz > 0) {
            LoadSeg seg{pOffset, addr, pFilesz, pMemsz, (pFlags & kPfX) != 0};
            if (seg.executable) {
                if (textSegs.size() >= kDolTextSections) {
                    return R::err({ErrorCode::InvalidData, "elf: too many text segments (>7)"});
                }
                textSegs.push_back(seg);
            } else {
                if (dataSegs.size() >= (kDolSections - kDolTextSections)) {
                    return R::err({ErrorCode::InvalidData, "elf: too many data segments (>11)"});
                }
                dataSegs.push_back(seg);
            }
        }
        if (pMemsz > pFilesz) {
            const u32 segBssStart = addr + pFilesz;
            const u32 segBssEnd = addr + pMemsz;
            if (bssEnd == 0) {
                bssBegin = segBssStart;
                bssEnd = segBssEnd;
            } else {
                bssBegin = std::min(bssBegin, segBssStart);
                bssEnd = std::max(bssEnd, segBssEnd);
            }
        }
    }

    if (textSegs.empty() && dataSegs.empty()) {
        return R::err({ErrorCode::InvalidData, "elf: no loadable file segments"});
    }

    // Check for overlapping PT_LOAD memory ranges.
    std::sort(memRanges.begin(), memRanges.end());
    for (std::size_t i = 1; i < memRanges.size(); ++i) {
        if (memRanges[i].first < memRanges[i - 1].second) {
            return R::err({ErrorCode::InvalidData, "elf: overlapping PT_LOAD segments"});
        }
    }

    // Calculate total DOL size (header + 32-byte aligned section payloads).
    u64 dolSize = kDolHeaderBytes;
    for (const auto& s : textSegs) dolSize = (dolSize + s.fileSize + 31u) & ~u64{31};
    for (const auto& s : dataSegs) dolSize = (dolSize + s.fileSize + 31u) & ~u64{31};
    if (dolSize > maxDolBytes) {
        return R::err({ErrorCode::InvalidData, "elf: converted DOL exceeds max size"});
    }

    std::vector<u8> dol(static_cast<std::size_t>(dolSize), 0);
    u32 cursor = kDolHeaderBytes;

    auto writeSec = [&](std::size_t idx, const LoadSeg& s) {
        putBe32(dol, 0x00 + idx * 4, cursor);
        putBe32(dol, 0x48 + idx * 4, s.vaddr);
        putBe32(dol, 0x90 + idx * 4, s.fileSize);
        std::memcpy(dol.data() + cursor, elf.data() + s.fileOffset, s.fileSize);
        cursor = (cursor + s.fileSize + 31u) & ~31u;
    };

    for (std::size_t i = 0; i < textSegs.size(); ++i) writeSec(i, textSegs[i]);
    for (std::size_t i = 0; i < dataSegs.size(); ++i) writeSec(kDolTextSections + i, dataSegs[i]);

    putBe32(dol, 0xD8, bssEnd > bssBegin ? bssBegin : 0u);
    putBe32(dol, 0xDC, bssEnd > bssBegin ? (bssEnd - bssBegin) : 0u);
    putBe32(dol, 0xE0, normalizeWiiAddr(entry));

    auto checked = checkDolImage(dol, maxDolBytes);
    if (!checked.isOk()) {
        return R::err(checked.error());
    }
    return R::ok(std::move(dol));
}

}  // namespace nl
