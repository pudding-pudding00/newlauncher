#pragma once
// Safe .zip homebrew package inspector and atomic SD installer for wiiload (M31).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/result.hpp"
#include "core/types.hpp"
#include "core/wiiload_proto.hpp"

namespace nl {

inline constexpr u64 kWiiloadMaxZipUnzipped = 32u * 1024 * 1024;

struct WiiloadZipPlan {
    std::string appName;     // non-empty when isBundle == true (from apps/<appName>/)
    std::string entryPath;   // path inside ZIP (e.g. "apps/demo/boot.dol" or "boot.dol")
    WiiloadKind entryKind = WiiloadKind::Dol;
    bool isBundle = false;
    u64 totalUnzipped = 0;
};

// Inspects an in-memory ZIP archive for wiiload:
// - Standard bundle: all files under apps/<appName>/ with boot.dol or boot.elf
// - Single-binary ZIP: root boot.dol or boot.elf (isBundle = false)
// Rejects path traversal, absolute paths, invalid app names, missing entry, or total > 32 MiB.
Result<WiiloadZipPlan> inspectWiiloadZip(std::span<const u8> zipBytes);

// Extracts a single entry from an in-memory ZIP archive.
Result<std::vector<u8>> extractWiiloadZipEntry(std::span<const u8> zipBytes, std::string_view entryPath);

// Atomically installs a bundle ZIP to <root>/apps/<appName>/ using .tmp staging and .old rollback.
// Returns the installed executable path (e.g. "sd:/apps/demo/boot.dol").
Result<std::string> installWiiloadZip(FileSystem& fs, std::span<const u8> zipBytes,
                                      const WiiloadZipPlan& plan, std::string_view root = "sd:");

}  // namespace nl
