/* newlauncher (M16): siehe nl_ntfs_extents.h. Erkennung laut Spike docs/superpowers/spikes/m16-dateisysteme.md. */
#include "nl_ntfs_extents.h"

#include "ntfsinternal.h"

int nl_ntfs_file_runs(const char* path, nl_ntfs_file_info* info, void (*cb)(void* ctx, const nl_ntfs_run* run),
                      void* ctx) {
    ntfs_vd* vd = ntfsGetVolume(path, false);
    if (!vd) return NL_NTFS_ERR_NOVOL;
    ntfsLock(vd);
    int rc = NL_NTFS_ERR_NOTFOUND;
    ntfs_inode* ni = ntfsOpenEntry(vd, path);
    if (ni) {
        ntfs_attr* na = ntfs_attr_open(ni, AT_DATA, AT_UNNAMED, 0);
        if (na) {
            info->clusterBytes = vd->vol->cluster_size;
            info->size = (uint64_t)na->data_size;
            const ATTR_FLAGS odd = ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE | ATTR_IS_ENCRYPTED;
            if (!NAttrNonResident(na) || (na->data_flags & odd) || na->initialized_size < na->data_size) {
                rc = NL_NTFS_ERR_UNSUPPORTED;
            } else if (ntfs_attr_map_whole_runlist(na) != 0) {
                rc = NL_NTFS_ERR_IO;
            } else {
                for (const runlist_element* rl = na->rl; rl && rl->length; ++rl) {
                    nl_ntfs_run run;
                    run.vcn = (uint64_t)rl->vcn;
                    run.hole = rl->lcn < 0;
                    run.lcn = run.hole ? 0 : (uint64_t)rl->lcn;
                    run.length = (uint64_t)rl->length;
                    cb(ctx, &run);
                }
                rc = NL_NTFS_OK;
            }
            ntfs_attr_close(na);
        }
        ntfsCloseEntry(vd, ni);
    }
    ntfsUnlock(vd);
    return rc;
}
