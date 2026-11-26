#include "platform/isfs_fs.hpp"

#include <gccore.h>
#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "core/nand_path.hpp"

namespace nl::platform {
namespace {

// Fehlercodes des IOS-Dateisystems.
constexpr s32 kFsAccess = -102;
constexpr s32 kFsExists = -105;
constexpr s32 kFsNotFound = -106;

constexpr u32 kChunk = 16 * 1024;  // Puffergröße für Lesen und Schreiben, Vielfaches von 32

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
using AlignedBuf = std::unique_ptr<u8, AlignedFree>;

AlignedBuf alignedAlloc(std::size_t bytes) {
    const std::size_t rounded = (bytes + 31) & ~std::size_t{31};
    return AlignedBuf(static_cast<u8*>(memalign(32, rounded == 0 ? 32 : rounded)));
}

// ISFS will Pfade in 32-Byte-ausgerichteten Puffern.
struct IsfsName {
    alignas(32) char s[ISFS_MAXPATH + 1];
};

bool toName(const std::string& p, IsfsName& out) {
    if (p.size() > ISFS_MAXPATH) return false;
    std::memcpy(out.s, p.c_str(), p.size() + 1);
    return true;
}

Error fsError(s32 rc, std::string_view what, std::string_view path) {
    const ErrorCode code = rc == kFsNotFound ? ErrorCode::NotFound : ErrorCode::IoError;
    char num[24];
    std::snprintf(num, sizeof num, " (%d): ", static_cast<int>(rc));
    std::string ctx(what);
    ctx += num;
    ctx += path;
    return Error{code, std::move(ctx)};
}

Error badPath(std::string_view path) {
    return Error{ErrorCode::Unsupported, "NAND-Pfad nicht erlaubt: " + std::string(path)};
}

std::string parentOf(const std::string& p) {
    const std::size_t s = p.rfind('/');
    return s == 0 || s == std::string::npos ? std::string("/") : p.substr(0, s);
}

struct Attr {
    u32 owner = 0;
    u16 group = 0;
    u8 attributes = 0, ownerPerm = 0, groupPerm = 0, otherPerm = 0;
};

s32 getAttr(const std::string& p, Attr& a) {
    IsfsName n;
    if (!toName(p, n)) return ISFS_EINVAL;
    return ISFS_GetAttr(n.s, &a.owner, &a.group, &a.attributes, &a.ownerPerm, &a.groupPerm, &a.otherPerm);
}

s32 setAttr(const std::string& p, const Attr& a) {
    IsfsName n;
    if (!toName(p, n)) return ISFS_EINVAL;
    return ISFS_SetAttr(n.s, a.owner, a.group, a.attributes, a.ownerPerm, a.groupPerm, a.otherPerm);
}

// Dateigröße, oder ein negativer ISFS-Fehler.
s32 fileSize(const std::string& p) {
    IsfsName n;
    if (!toName(p, n)) return ISFS_EINVAL;
    const s32 fd = ISFS_Open(n.s, ISFS_OPEN_READ);
    if (fd < 0) return fd;
    struct alignas(32) Stats {
        fstats st;
    } stats{};
    const s32 rc = ISFS_GetFileStats(fd, &stats.st);
    ISFS_Close(fd);
    return rc < 0 ? rc : static_cast<s32>(stats.st.file_length);
}

bool isDirectory(const std::string& p) {
    IsfsName n;
    if (!toName(p, n)) return false;
    u32 count = 0;
    return ISFS_ReadDir(n.s, nullptr, &count) >= 0;
}

}  // namespace

Result<std::vector<DirEntry>> IsfsFileSystem::list(std::string_view dir) {
    using R = Result<std::vector<DirEntry>>;
    const auto p = isfsPath(dir);
    IsfsName n;
    if (!p || !toName(*p, n)) return R::err(badPath(dir));
    u32 count = 0;
    if (const s32 rc = ISFS_ReadDir(n.s, nullptr, &count); rc < 0) return R::err(fsError(rc, "ReadDir", dir));
    std::vector<DirEntry> out;
    if (count == 0) return R::ok(std::move(out));
    AlignedBuf names = alignedAlloc(static_cast<std::size_t>(count) * (kIsfsMaxName + 1));
    if (!names) return R::err(Error{ErrorCode::IoError, "kein Speicher"});
    if (const s32 rc = ISFS_ReadDir(n.s, reinterpret_cast<char*>(names.get()), &count); rc < 0)
        return R::err(fsError(rc, "ReadDir", dir));
    // Die Namen stehen nullterminiert hintereinander.
    const char* cur = reinterpret_cast<const char*>(names.get());
    for (u32 i = 0; i < count; ++i) {
        const std::string name(cur);
        cur += name.size() + 1;
        const std::string child = joinPath(*p, name);
        const bool dirFlag = isDirectory(child);
        const s32 size = dirFlag ? 0 : fileSize(child);
        out.push_back(DirEntry{name, dirFlag, size > 0 ? static_cast<u64>(size) : 0, 0});
    }
    return R::ok(std::move(out));
}

Result<std::size_t> IsfsFileSystem::read(std::string_view file, u64 offset, std::span<u8> out) {
    using R = Result<std::size_t>;
    const auto p = isfsPath(file);
    IsfsName n;
    if (!p || !toName(*p, n)) return R::err(badPath(file));
    const s32 fd = ISFS_Open(n.s, ISFS_OPEN_READ);
    if (fd < 0) return R::err(fsError(fd, "Open", file));
    std::size_t done = 0;
    if (!out.empty()) {
        AlignedBuf buf = alignedAlloc(kChunk);
        if (!buf) {
            ISFS_Close(fd);
            return R::err(Error{ErrorCode::IoError, "kein Speicher"});
        }
        if (ISFS_Seek(fd, static_cast<s32>(offset), SEEK_SET) < 0) {
            ISFS_Close(fd);
            return R::ok(0);
        }
        while (done < out.size()) {
            const u32 want = static_cast<u32>(std::min<std::size_t>(kChunk, out.size() - done));
            const s32 got = ISFS_Read(fd, buf.get(), want);
            if (got < 0) {
                ISFS_Close(fd);
                return R::err(fsError(got, "Read", file));
            }
            std::memcpy(out.data() + done, buf.get(), static_cast<std::size_t>(got));
            done += static_cast<std::size_t>(got);
            if (static_cast<u32>(got) < want) break;
        }
    }
    ISFS_Close(fd);
    return R::ok(done);
}

Result<std::vector<u8>> IsfsFileSystem::readAll(std::string_view file, std::size_t maxBytes) {
    using R = Result<std::vector<u8>>;
    const auto p = isfsPath(file);
    if (!p) return R::err(badPath(file));
    const s32 size = fileSize(*p);
    if (size < 0) return R::err(fsError(size, "Stat", file));
    if (static_cast<std::size_t>(size) > maxBytes)
        return R::err(Error{ErrorCode::InvalidData, "zu groß: " + std::string(file)});
    std::vector<u8> data(static_cast<std::size_t>(size));
    auto r = read(file, 0, data);
    if (!r.isOk()) return R::err(r.error());
    if (r.value() != data.size()) return R::err(Error{ErrorCode::IoError, "kurz gelesen: " + std::string(file)});
    return R::ok(std::move(data));
}

Result<void> IsfsFileSystem::writeAtomic(std::string_view file, std::span<const u8> data) {
    using R = Result<void>;
    const auto p = isfsPath(file);
    if (!writable_ || !p || !isSaveDataPath(*p)) return R::err(badPath(file));

    // Rechte der alten Datei übernehmen; neue Dateien erben vom Ordner (Eigentümer = Spiel).
    Attr a;
    if (getAttr(*p, a) < 0) {
        const std::string parent = parentOf(*p);
        if (const s32 rc = getAttr(parent, a); rc < 0) return R::err(fsError(rc, "GetAttr", parent));
        a.attributes = 0;
    }

    const std::string tmp = nandTempPath(*p);
    IsfsName tn, dn;
    if (!toName(tmp, tn) || !toName(*p, dn)) return R::err(badPath(file));
    (void)ISFS_Delete(tn.s);  // Rest eines abgebrochenen Laufs
    if (const s32 rc = ISFS_CreateFile(tn.s, a.attributes, a.ownerPerm, a.groupPerm, a.otherPerm); rc < 0)
        return R::err(fsError(rc, "CreateFile", tmp));
    auto fail = [&](s32 rc, std::string_view what) {
        (void)ISFS_Delete(tn.s);
        return R::err(fsError(rc, what, file));
    };
    const s32 fd = ISFS_Open(tn.s, ISFS_OPEN_WRITE);
    if (fd < 0) return fail(fd, "Open");
    AlignedBuf buf = alignedAlloc(kChunk);
    if (!buf) {
        ISFS_Close(fd);
        return fail(ISFS_ENOMEM, "Speicher");
    }
    for (std::size_t off = 0; off < data.size(); off += kChunk) {
        const u32 n = static_cast<u32>(std::min<std::size_t>(kChunk, data.size() - off));
        std::memcpy(buf.get(), data.data() + off, n);
        const s32 w = ISFS_Write(fd, buf.get(), n);
        if (w != static_cast<s32>(n)) {
            ISFS_Close(fd);
            return fail(w < 0 ? w : ISFS_EINVAL, "Write");
        }
    }
    if (const s32 rc = ISFS_Close(fd); rc < 0) return fail(rc, "Close");
    // Eigentümer und Gruppe gehören dem Spiel, nicht dem Launcher.
    if (const s32 rc = setAttr(tmp, a); rc < 0) return fail(rc, "SetAttr");

    // S1 offen: ob ISFS_Rename ein vorhandenes Ziel ersetzt, ist auf der Wii zu bestätigen. Wenn nicht, wird das Ziel
    // gelöscht und erneut umbenannt; für diese Lücke hält das Journal aus save_backup den alten Stand.
    s32 rc = ISFS_Rename(tn.s, dn.s);
    if (rc == kFsExists) {
        if (const s32 d = ISFS_Delete(dn.s); d < 0) return fail(d, "Delete");
        rc = ISFS_Rename(tn.s, dn.s);
    }
    if (rc < 0) return fail(rc, rc == kFsAccess ? "Rename (Rechte)" : "Rename");
    return R::ok();
}

Result<void> IsfsFileSystem::makeDirs(std::string_view dir) {
    using R = Result<void>;
    const auto p = isfsPath(dir);
    if (!writable_ || !p || !isSaveDataDir(*p)) return R::err(badPath(dir));
    // /title/<hi>/<lo>/data muss schon existieren; angelegt wird nur darunter, mit den Rechten von data.
    std::size_t pos = 0;
    for (int i = 0; i < 4 && pos != std::string::npos; ++i) pos = p->find('/', pos + 1);
    const std::string dataDir = p->substr(0, std::min(pos, p->size()));
    Attr a;
    if (const s32 rc = getAttr(dataDir, a); rc < 0) return R::err(fsError(rc, "GetAttr", dataDir));
    while (pos != std::string::npos) {
        pos = p->find('/', pos + 1);
        const std::string sub = p->substr(0, std::min(pos, p->size()));
        if (isDirectory(sub)) continue;
        IsfsName n;
        if (!toName(sub, n)) return R::err(badPath(sub));
        if (const s32 rc = ISFS_CreateDir(n.s, 0, a.ownerPerm, a.groupPerm, a.otherPerm); rc < 0 && rc != kFsExists)
            return R::err(fsError(rc, "CreateDir", sub));
        if (const s32 rc = setAttr(sub, a); rc < 0) return R::err(fsError(rc, "SetAttr", sub));
    }
    return R::ok();
}

Result<void> IsfsFileSystem::removeFile(std::string_view file) {
    using R = Result<void>;
    const auto p = isfsPath(file);
    IsfsName n;
    if (!writable_ || !p || !isSaveDataPath(*p) || !toName(*p, n)) return R::err(badPath(file));
    if (isDirectory(*p)) return R::err(Error{ErrorCode::InvalidData, "ist ein Ordner: " + std::string(file)});
    const s32 rc = ISFS_Delete(n.s);
    if (rc < 0 && rc != kFsNotFound) return R::err(fsError(rc, "Delete", file));
    return R::ok();
}

Result<void> IsfsFileSystem::renameFile(std::string_view from, std::string_view) {
    return Result<void>::err(Error{ErrorCode::Unsupported, "NAND: Umbenennen nicht unterstützt: " + std::string(from)});
}

}  // namespace nl::platform
