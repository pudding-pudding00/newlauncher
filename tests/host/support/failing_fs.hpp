#pragma once
// Fehlerinjektion für Host-Tests (M26): leitet alles an ein MemFileSystem weiter; der (failWriteAfter + 1)-te
// writeAtomic-Aufruf scheitert, alle anderen gehen durch.

#include "support/mem_fs.hpp"

namespace nl::test {

class FailingFs final : public FileSystem {
public:
    explicit FailingFs(MemFileSystem& inner) : inner_(inner) {}

    int failWriteAfter = -1;  // -1 = nie
    int writeCalls = 0;
    std::vector<std::string> readAllPaths;  // jeder readAll-Aufruf, in Reihenfolge

    Result<std::vector<DirEntry>> list(std::string_view dir) override { return inner_.list(dir); }
    Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) override {
        return inner_.read(file, offset, out);
    }
    Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) override {
        readAllPaths.emplace_back(file);
        return inner_.readAll(file, maxBytes);
    }
    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override {
        if (writeCalls++ == failWriteAfter) return Result<void>::err(Error{ErrorCode::IoError, "injiziert"});
        return inner_.writeAtomic(file, data);
    }
    Result<void> makeDirs(std::string_view dir) override { return inner_.makeDirs(dir); }
    Result<void> removeFile(std::string_view file) override { return inner_.removeFile(file); }
    Result<void> renameFile(std::string_view from, std::string_view to) override { return inner_.renameFile(from, to); }

private:
    MemFileSystem& inner_;
};

}  // namespace nl::test
