#include "platform/fs_extents.hpp"

#include <ogc/usbstorage.h>

#include <string>
#include <vector>

#include "platform/log.hpp"

#ifdef NL_FS_NTFS
#include <nl_ntfs_extents.h>
#endif

namespace nl::platform {

u32 deviceSectorBytes(const Device& dev) {
    if (!dev.slot.usb || deviceTable().sdAsDisk()) return 512;
    return __io_usbstorage_sector_size != 0 ? __io_usbstorage_sector_size : 512;
}

Result<FilePart, ExtentError> fileExtentsFor(const Device& dev, std::string_view path) {
    using R = Result<FilePart, ExtentError>;
#ifdef NL_FS_NTFS
    if (dev.slot.kind != FsKind::Ntfs) return R::err(ExtentError::Unsupported);
    std::vector<FsRun> runs;
    nl_ntfs_file_info info{};
    const std::string p(path);
    const int rc = nl_ntfs_file_runs(
        p.c_str(), &info,
        [](void* ctx, const nl_ntfs_run* run) {
            static_cast<std::vector<FsRun>*>(ctx)->push_back({run->vcn, run->lcn, run->length, run->hole != 0});
        },
        &runs);
    if (rc != NL_NTFS_OK) {
        log::line("NL_NTFS_RUNS %s rc=%d", p.c_str(), rc);
        // Nicht gefunden oder unlesbar: Der Aufrufer hat die Datei per stat gesehen, also ist sie kaputt.
        return R::err(rc == NL_NTFS_ERR_UNSUPPORTED ? ExtentError::Unsupported : ExtentError::Gap);
    }
    auto ext = runsToExtents(runs, info.clusterBytes, dev.slot.lba, deviceSectorBytes(dev), info.size);
    if (!ext.isOk()) return R::err(ext.error());
    return R::ok(FilePart{info.size, std::move(ext.value())});
#else
    (void)dev;
    (void)path;
    return R::err(ExtentError::Unsupported);
#endif
}

}  // namespace nl::platform
