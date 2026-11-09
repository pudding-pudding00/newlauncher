#include "platform/std_fs.hpp"

#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace nl::platform {

namespace {

// "sd:" → "sd:/"; libfat braucht nach dem Gerät einen Schrägstrich.
std::string osPath(std::string_view p) {
    std::string s(p);
    if (!s.empty() && s.back() == ':') s += '/';
    return s;
}

Error ioError(std::string_view what, std::string_view path) {
    std::string ctx(what);
    ctx += ": ";
    ctx += path;
    return Error{ErrorCode::IoError, ctx};
}

}  // namespace

Result<std::vector<DirEntry>> StdFileSystem::list(std::string_view dir) {
    using R = Result<std::vector<DirEntry>>;
    const std::string path = osPath(dir);
    DIR* d = opendir(path.c_str());
    if (d == nullptr) return R::err(Error{ErrorCode::NotFound, path});
    std::vector<DirEntry> out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        struct stat st{};
        const std::string full = joinPath(path, name);
        if (stat(full.c_str(), &st) != 0) continue;
        out.push_back(DirEntry{name, S_ISDIR(st.st_mode), static_cast<u64>(static_cast<std::make_unsigned_t<off_t>>(st.st_size)),
                               static_cast<u32>(st.st_mtime)});
    }
    closedir(d);
    return R::ok(std::move(out));
}

Result<std::size_t> StdFileSystem::read(std::string_view file, u64 offset, std::span<u8> out) {
    using R = Result<std::size_t>;
    const std::string path = osPath(file);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return R::err(ioError("öffnen", path));
    if (offset > 0 && std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) {
        std::fclose(f);
        return R::ok(0);
    }
    const std::size_t n = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return R::ok(n);
}

Result<std::vector<u8>> StdFileSystem::readAll(std::string_view file, std::size_t maxBytes) {
    using R = Result<std::vector<u8>>;
    const std::string path = osPath(file);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return R::err(Error{ErrorCode::NotFound, path});
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0 || static_cast<std::size_t>(size) > maxBytes) {
        std::fclose(f);
        return R::err(Error{ErrorCode::InvalidData, "zu groß: " + path});
    }
    std::vector<u8> data(static_cast<std::size_t>(size));
    const std::size_t n = data.empty() ? 0 : std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (n != data.size()) return R::err(ioError("lesen", path));
    return R::ok(std::move(data));
}

Result<void> StdFileSystem::writeAtomic(std::string_view file, std::span<const u8> data) {
    const std::string path = osPath(file);
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) return Result<void>::err(ioError("schreiben", tmp));
    const std::size_t n = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    const bool ok = std::fclose(f) == 0 && n == data.size();
    if (!ok) {
        std::remove(tmp.c_str());
        return Result<void>::err(ioError("schreiben", tmp));
    }
    std::remove(path.c_str());  // FAT: rename überschreibt keine vorhandene Datei
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return Result<void>::err(ioError("umbenennen", path));
    return Result<void>::ok();
}

Result<void> StdFileSystem::makeDirs(std::string_view dir) {
    const std::string path = osPath(dir);
    const std::size_t start = path.find(":/");
    for (std::size_t i = start == std::string::npos ? 0 : start + 2; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (!part.empty() && part.back() != ':') mkdir(part.c_str(), 0777);
        }
    }
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return Result<void>::err(ioError("Ordner anlegen", path));
    return Result<void>::ok();
}

}  // namespace nl::platform
