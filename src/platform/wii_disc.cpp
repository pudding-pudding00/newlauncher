#include "platform/wii_disc.hpp"

#include <gccore.h>
#include <malloc.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "core/wii_title.hpp"
#include "platform/log.hpp"

namespace nl::platform {

namespace {

constexpr u32 kBounceSectors = 64;

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
using AlignedBuf = std::unique_ptr<u8, AlignedFree>;

AlignedBuf alignedAlloc(std::size_t bytes) {
    const std::size_t rounded = (bytes + 31) & ~std::size_t{31};
    return AlignedBuf(static_cast<u8*>(memalign(32, rounded == 0 ? 32 : rounded)));
}

struct Part {
    std::string path;
    u32 startCluster;  // libfat liefert ihn in st_ino (_FAT_directory_entryStat)
    FilePart file;
};

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char c = s[s.size() - suffix.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

// Gerätesektor für Dateisektor `s`; 0, wenn außerhalb der Extents.
u32 lbaFor(const std::vector<Extent>& ext, u32 s) {
    for (const Extent& e : ext) {
        if (s >= e.fileSector && s - e.fileSector < e.count) return e.lba + (s - e.fileSector);
    }
    return 0;
}

// Ein Sektor roh gegen libfat: gleiche Bytes am selben Dateioffset?
bool verifySector(FileSystem& fs, BlockDevice& dev, const Part& p, u32 sector) {
    const u32 lba = lbaFor(p.file.extents, sector);
    if (lba == 0) return false;
    u8 raw[512];
    u8 viaFs[512];
    if (!dev.read(lba, 1, raw)) return false;
    auto got = fs.read(p.path, u64{sector} * 512, viaFs);
    if (!got.isOk() || got.value() == 0) return false;
    return std::memcmp(raw, viaFs, got.value()) == 0;
}

bool verifyPart(FileSystem& fs, BlockDevice& dev, const Part& p) {
    if (p.file.size == 0) return true;
    const u32 last = static_cast<u32>((p.file.size + 511) / 512 - 1);
    return verifySector(fs, dev, p, 0) && verifySector(fs, dev, p, last);
}

}  // namespace

RawDevice::RawDevice(bool usb)
    : io_(usb ? &__io_usbstorage : &__io_wiisd),
      sectorSize_(usb && __io_usbstorage_sector_size != 0 ? __io_usbstorage_sector_size : 512),
      buf_(static_cast<u8*>(memalign(32, kBounceSectors * sectorSize_))) {}

RawDevice::~RawDevice() { std::free(buf_); }

bool RawDevice::read(u32 lba, u32 count, std::span<u8> out) {
    if (buf_ == nullptr || out.size() < std::size_t{count} * sectorSize_) return false;
    while (count > 0) {
        const u32 n = std::min(count, kBounceSectors);
        if (!io_->readSectors(lba, n, buf_)) return false;
        std::memcpy(out.data(), buf_, std::size_t{n} * sectorSize_);
        out = out.subspan(std::size_t{n} * sectorSize_);
        lba += n;
        count -= n;
    }
    return true;
}

Result<FragList, LaunchError> computeWiiFrags(FileSystem& fs, const Game& g, const Device& device, u32* partLbaOut) {
    using R = Result<FragList, LaunchError>;
    const std::string libRoot = device.slot.name + ":";
    const std::string main = joinPath(libRoot, g.path != nullptr ? g.path : "");
    const bool wbfs = endsWithNoCase(main, ".wbfs");

    std::vector<Part> parts;
    for (int i = 0; i <= (wbfs ? 9 : 0); ++i) {
        std::string path = main;
        if (i > 0) path = main.substr(0, main.size() - 1) + static_cast<char>('0' + i);  // .wbfs → .wbf1 …
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) {
            if (i == 0) return R::err({LaunchFailure::RomMissing, main});
            break;
        }
        parts.push_back({path, static_cast<u32>(st.st_ino), {static_cast<u64>(st.st_size), {}}});
    }

    RawDevice dev(device.slot.usb);
    if (device.slot.lba > 0xFFFFFFFFull) return R::err({LaunchFailure::WiiFileBroken, main});
    auto vol = findFatVolumeAt(dev, static_cast<u32>(device.slot.lba));
    if (!vol.isOk()) return R::err(fromFatError(vol.error(), main));
    if (partLbaOut != nullptr) *partLbaOut = vol.value().partLba;

    for (Part& p : parts) {
        auto ext = fileExtents(dev, vol.value(), p.startCluster, p.file.size);
        if (!ext.isOk()) return R::err(fromFatError(ext.error(), p.path));
        p.file.extents = std::move(ext.value());
    }

    Result<FragList, FragError> list = Result<FragList, FragError>::err(FragError::WbfsInvalid);
    if (wbfs) {
        u8 first[512];
        auto got = fs.read(main, 0, first);
        if (!got.isOk() || got.value() < 512) return R::err({LaunchFailure::WiiFileBroken, main});
        const u8 shift = first[9];
        std::vector<u8> head(shift >= 15 && shift <= 30 ? wbfsHeadBytes(shift) : 512);
        auto all = fs.read(main, 0, head);
        if (!all.isOk() || all.value() < head.size()) return R::err({LaunchFailure::WiiFileBroken, main});
        auto layout = parseWbfsHead(head);
        if (!layout.isOk()) return R::err(fromFragError(layout.error(), main));
        std::vector<FilePart> files;
        for (const Part& p : parts) files.push_back(p.file);
        list = buildWbfsFragList(layout.value(), files);
    } else {
        list = buildIsoFragList(parts.front().file);
    }
    if (!list.isOk()) {
        // Fehlt ein Teil, nennt der Dialog den ersten fehlenden (.wbf1 …), nicht die Hauptdatei.
        std::string context = main;
        if (list.error() == FragError::PartMissing && wbfs && parts.size() <= 9) {
            context = main.substr(0, main.size() - 1) + static_cast<char>('0' + parts.size());
        }
        return R::err(fromFragError(list.error(), context));
    }

    const bool ok = verifyPart(fs, dev, parts.front()) && verifyPart(fs, dev, parts.back());
    if (!ok) {
        log::line("NL_FRAG_VERIFY fail %s", main.c_str());
        return R::err({LaunchFailure::WiiFileBroken, main});
    }
    log::line("NL_FRAG_VERIFY ok");
    return R::ok(std::move(list.value()));
}

std::optional<CiosEntry> readCiosEntry(u8 slot) {
    const u64 title = (u64{1} << 32) | slot;
    u32 size = 0;
    if (ES_GetStoredTMDSize(title, &size) < 0 || size == 0) return std::nullopt;
    AlignedBuf tmdBuf = alignedAlloc(size);
    if (!tmdBuf || ES_GetStoredTMD(title, reinterpret_cast<signed_blob*>(tmdBuf.get()), size) < 0) return std::nullopt;
    auto tmd = parseTmd(std::span<const u8>(tmdBuf.get(), size));
    if (!tmd.isOk()) return std::nullopt;
    const TmdInfo& t = tmd.value();

    // Wie WiiFlow (IOS_GetInfo, TMD+0x1E4): d2x legt ios_info_t an den Anfang des ersten Content-Eintrags.
    std::optional<IosInfo> info;
    // nand_titles hält ISFS für die ganze Sitzung offen; ISFS_Initialize ist dann ein No-Op.
    if (!t.contents.empty() && ISFS_Initialize() >= 0) {
        char path[64];
        std::snprintf(path, sizeof path, "/title/00000001/%08x/content/%08x.app", static_cast<unsigned>(slot),
                      static_cast<unsigned>(t.contents.front().id));
        const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
        if (fd >= 0) {
            AlignedBuf head = alignedAlloc(0x40);
            if (head && ISFS_Read(fd, head.get(), 0x40) == 0x40) info = parseIosInfo(std::span<const u8>(head.get(), 0x40));
            ISFS_Close(fd);
        }
    }

    CiosEntry e;
    e.slot = slot;
    e.titleVersion = t.titleVersion;
    e.kind = classifySlot(t.titleVersion, static_cast<u32>(t.contents.size()), info);
    if (e.kind == CiosKind::D2x) {
        e.base = info->baseios;
        e.name = info->name;
        e.version = info->versionString;
    }
    return e;
}

std::vector<CiosEntry> scanCios() {
    u32 count = 0;
    if (ES_GetNumTitles(&count) < 0 || count == 0) return {};
    AlignedBuf buf = alignedAlloc(count * sizeof(u64));
    if (!buf) return {};
    auto* ids = reinterpret_cast<u64*>(buf.get());
    if (ES_GetTitles(ids, count) < 0) return {};
    std::vector<u8> slots;
    for (u32 i = 0; i < count; ++i) {
        const u64 id = ids[i];
        const u32 lo = static_cast<u32>(id);
        if ((id >> 32) == 1 && lo >= kCiosFirstSlot && lo <= kCiosLastSlot) slots.push_back(static_cast<u8>(lo));
    }
    std::sort(slots.begin(), slots.end());
    std::vector<CiosEntry> out;
    for (u8 slot : slots)
        if (auto e = readCiosEntry(slot)) out.push_back(std::move(*e));
    return out;
}

}  // namespace nl::platform
