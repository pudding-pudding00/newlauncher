# Local Patches to Vendored `libntfs`

All patches below resolve GitHub Code Scanning (CodeQL) alerts in `third_party/libntfs/upstream/source/` while preserving upstream behavior:

1. **Pointer-swap before `free` on `base_ni->extent_nis` reallocation** (`cpp/use-after-free`, alerts `#10`, `#11`, `#12`):
   - `upstream/source/inode.c` (`ntfs_extent_inode_open`)
   - `upstream/source/mft.c` (`ntfs_mft_record_alloc`, two sites)
   - Save `old_extents = base_ni->extent_nis`, copy from `old_extents`, assign `base_ni->extent_nis = extent_nis`, then `free(old_extents)` so `base_ni->extent_nis` never holds a freed pointer.

2. **Widen 32-bit multiplications to `size_t`** (`cpp/integer-multiplication-cast-to-long`, alerts `#14`, `#15`):
   - `upstream/source/cache2.c` (`_NTFS_cache_constructor`): `(size_t)sectorsPerPage * (size_t)bytesPerSector`.
   - `upstream/source/mft.c` (`ntfs_mft_records_write`): `(size_t)cnt * (size_t)vol->mft_record_size`.
