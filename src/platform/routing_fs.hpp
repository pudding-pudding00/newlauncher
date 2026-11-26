#pragma once
// Leitet "nand:"-Pfade an das ISFS-Dateisystem und alles andere an stdio/libfat (M26).

#include "core/fs.hpp"

namespace nl::platform {

class RoutingFileSystem final : public FileSystem {
public:
    RoutingFileSystem(FileSystem& nand, FileSystem& other) : nand_(nand), other_(other) {}

    Result<std::vector<DirEntry>> list(std::string_view dir) override { return pick(dir).list(dir); }
    Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) override {
        return pick(file).read(file, offset, out);
    }
    Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) override {
        return pick(file).readAll(file, maxBytes);
    }
    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override {
        return pick(file).writeAtomic(file, data);
    }
    Result<void> makeDirs(std::string_view dir) override { return pick(dir).makeDirs(dir); }
    Result<void> removeFile(std::string_view file) override { return pick(file).removeFile(file); }
    Result<void> renameFile(std::string_view from, std::string_view to) override {
        if (isNand(from) != isNand(to))
            return Result<void>::err(Error{ErrorCode::Unsupported, "Umbenennen über Gerätegrenzen"});
        return pick(from).renameFile(from, to);
    }

private:
    static bool isNand(std::string_view p) { return p.substr(0, 5) == "nand:"; }
    FileSystem& pick(std::string_view p) { return isNand(p) ? nand_ : other_; }

    FileSystem& nand_;
    FileSystem& other_;
};

}  // namespace nl::platform
