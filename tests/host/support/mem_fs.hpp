#pragma once
// In-Memory-Dateisystem für Host-Tests. Ordner entstehen implizit aus Dateipfaden.

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "core/fs.hpp"

namespace nl::test {

class MemFileSystem final : public FileSystem {
public:
    struct File {
        std::vector<u8> data;
        u32 mtime = 0;
    };

    void addFile(std::string path, std::vector<u8> data, u32 mtime = 0) {
        addParents(path);
        files_[path] = File{std::move(data), mtime};
    }
    void addDir(std::string path, u32 mtime = 0) {
        addParents(path);
        dirs_[path] = mtime;
    }
    void remove(const std::string& path) { files_.erase(path); }
    int reads = 0;   // Anzahl read()-Aufrufe (für Tests zum Header-Lesen)
    int writes = 0;  // Anzahl writeAtomic()/makeDirs()-Aufrufe

    Result<std::vector<DirEntry>> list(std::string_view dir) override {
        const std::string d(dir);
        if (dirs_.find(d) == dirs_.end()) return Result<std::vector<DirEntry>>::err(Error{ErrorCode::NotFound, d});
        std::vector<DirEntry> out;
        for (const auto& [path, mtime] : dirs_)
            if (parentOf(path) == d) out.push_back(DirEntry{nameOf(path), true, 0, mtime});
        for (const auto& [path, f] : files_)
            if (parentOf(path) == d) out.push_back(DirEntry{nameOf(path), false, f.data.size(), f.mtime});
        return Result<std::vector<DirEntry>>::ok(std::move(out));
    }

    Result<std::size_t> read(std::string_view file, u64 offset, std::span<u8> out) override {
        ++reads;
        auto it = files_.find(std::string(file));
        if (it == files_.end()) return Result<std::size_t>::err(Error{ErrorCode::NotFound, std::string(file)});
        const auto& data = it->second.data;
        if (offset >= data.size()) return Result<std::size_t>::ok(0);
        const std::size_t n = std::min<std::size_t>(out.size(), data.size() - static_cast<std::size_t>(offset));
        std::memcpy(out.data(), data.data() + offset, n);
        return Result<std::size_t>::ok(n);
    }

    Result<std::vector<u8>> readAll(std::string_view file, std::size_t maxBytes) override {
        auto it = files_.find(std::string(file));
        if (it == files_.end()) return Result<std::vector<u8>>::err(Error{ErrorCode::NotFound, std::string(file)});
        if (it->second.data.size() > maxBytes) return Result<std::vector<u8>>::err(Error{ErrorCode::InvalidData, "zu groß"});
        return Result<std::vector<u8>>::ok(it->second.data);
    }

    Result<void> writeAtomic(std::string_view file, std::span<const u8> data) override {
        ++writes;
        if (failWrites) return Result<void>::err(Error{ErrorCode::IoError, "schreibgeschützt"});
        addFile(std::string(file), std::vector<u8>(data.begin(), data.end()));
        return Result<void>::ok();
    }

    Result<void> makeDirs(std::string_view dir) override {
        ++writes;
        addDir(std::string(dir));
        return Result<void>::ok();
    }

    Result<void> removeFile(std::string_view file) override {
        ++writes;
        if (failWrites) return Result<void>::err(Error{ErrorCode::IoError, "schreibgeschützt"});
        files_.erase(std::string(file));
        return Result<void>::ok();
    }

    Result<void> renameFile(std::string_view from, std::string_view to) override {
        ++writes;
        const int n = renameCount++;
        if (failWrites || n == failRenameAt) return Result<void>::err(Error{ErrorCode::IoError, "umbenennen"});
        auto it = files_.find(std::string(from));
        if (it == files_.end()) return Result<void>::err(Error{ErrorCode::NotFound, std::string(from)});
        if (files_.count(std::string(to)) != 0) return Result<void>::err(Error{ErrorCode::IoError, "Ziel existiert"});
        File f = std::move(it->second);
        files_.erase(it);
        addFile(std::string(to), std::move(f.data), f.mtime);
        renames.emplace_back(std::string(from), std::string(to));
        return Result<void>::ok();
    }

    // Alle Dateipfade, die mit prefix beginnen.
    std::vector<std::string> filesUnder(const std::string& prefix) const {
        std::vector<std::string> out;
        for (const auto& [path, f] : files_)
            if (path.rfind(prefix, 0) == 0) out.push_back(path);
        return out;
    }

    std::vector<std::pair<std::string, std::string>> renames;  // erfolgreiche Umbenennungen in Reihenfolge
    int renameCount = 0;
    int failRenameAt = -1;  // n-ter renameFile-Aufruf (ab 0) schlägt fehl

    const File* file(const std::string& path) const {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : &it->second;
    }
    bool failWrites = false;

private:
    static std::string parentOf(const std::string& p) {
        const std::size_t s = p.rfind('/');
        return s == std::string::npos ? std::string() : p.substr(0, s);
    }
    static std::string nameOf(const std::string& p) {
        const std::size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }
    void addParents(const std::string& path) {
        for (std::string p = parentOf(path); !p.empty(); p = parentOf(p)) {
            if (dirs_.find(p) == dirs_.end()) dirs_[p] = 0;
            if (p.back() == ':') break;  // Gerätewurzel "sd:"
        }
    }

    std::map<std::string, File> files_;
    std::map<std::string, u32> dirs_;
};

}  // namespace nl::test
