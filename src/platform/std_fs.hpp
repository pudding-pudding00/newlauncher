#pragma once
// FileSystem über newlib-stdio und dirent (libfat-Geräte "sd:" und "usb:").

#include "core/fs.hpp"

namespace nl::platform {

class StdFileSystem final : public FileSystem {
public:
    Result<std::vector<DirEntry>> list(std::string_view dir) override;
    Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) override;
    Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) override;
    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override;
    Result<void> makeDirs(std::string_view dir) override;
    Result<void> removeFile(std::string_view file) override;
    Result<void> renameFile(std::string_view from, std::string_view to) override;
};

}  // namespace nl::platform
