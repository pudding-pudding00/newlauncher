#include "core/zip_reader.hpp"

#include <algorithm>

#include "core/crc32.hpp"
#include "core/inflate.hpp"

namespace nl {

namespace {

constexpr u32 kSigLocal = 0x04034b50u, kSigCentral = 0x02014b50u, kSigEnd = 0x06054b50u;
constexpr std::size_t kEndSize = 22, kCentralSize = 46, kLocalSize = 30;
constexpr std::size_t kMaxComment = 0xFFFF;
constexpr u32 kMaxCentralBytes = 4u * 1024 * 1024;

u32 le16(const u8* p) { return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8); }
u32 le32(const u8* p) { return le16(p) | (le16(p + 2) << 16); }

Error bad(std::string what) { return Error{ErrorCode::InvalidData, std::move(what)}; }

bool readExact(FileSystem& fs, std::string_view file, u64 offset, std::span<u8> out) {
    auto r = fs.read(file, offset, out);
    return r.isOk() && r.value() == out.size();
}

}  // namespace

bool isSafeZipPath(std::string_view name) {
    if (name.empty() || name.front() == '/') return false;
    for (char c : name)
        if (c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
    if (name.back() == '/') name.remove_suffix(1);  // Ordner-Eintrag
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = name.find('/', start);
        const std::string_view seg = name.substr(start, slash == std::string_view::npos ? name.npos : slash - start);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (slash == std::string_view::npos) return true;
        start = slash + 1;
    }
}

Result<std::vector<ZipEntry>> readZipDirectory(FileSystem& fs, std::string_view zip, u64 zipSize) {
    using R = Result<std::vector<ZipEntry>>;
    if (zipSize < kEndSize) return R::err(bad("kein ZIP-Verzeichnisende"));

    // Verzeichnisende rückwärts suchen; der Kommentar dahinter muss genau bis zum Dateiende reichen.
    const std::size_t tailLen = static_cast<std::size_t>(std::min<u64>(zipSize, kEndSize + kMaxComment));
    std::vector<u8> tail(tailLen);
    if (!readExact(fs, zip, zipSize - tailLen, tail)) return R::err(Error{ErrorCode::IoError, std::string(zip)});
    std::size_t end = std::string_view::npos;
    for (std::size_t i = tailLen - kEndSize + 1; i-- > 0;) {
        if (le32(&tail[i]) == kSigEnd && i + kEndSize + le16(&tail[i + 20]) == tailLen) {
            end = i;
            break;
        }
    }
    if (end == std::string_view::npos) return R::err(bad("kein ZIP-Verzeichnisende"));
    const u8* eocd = &tail[end];
    const u32 count = le16(eocd + 10);
    const u32 cdSize = le32(eocd + 12);
    const u32 cdOffset = le32(eocd + 16);
    const u64 eocdPos = zipSize - tailLen + end;
    if (le16(eocd + 4) != 0 || le16(eocd + 6) != 0 || le16(eocd + 8) != count)
        return R::err(bad("mehrteiliges ZIP"));
    if (count > kMaxZipEntries) return R::err(bad("zu viele ZIP-Einträge"));
    if (cdSize > kMaxCentralBytes || static_cast<u64>(cdOffset) + cdSize > eocdPos)
        return R::err(bad("ZIP-Verzeichnis ungültig"));

    std::vector<u8> cd(cdSize);
    if (!readExact(fs, zip, cdOffset, cd)) return R::err(bad("ZIP-Verzeichnis unvollständig"));
    std::vector<ZipEntry> out;
    out.reserve(count);
    std::size_t p = 0;
    for (u32 i = 0; i < count; ++i) {
        if (p + kCentralSize > cd.size() || le32(&cd[p]) != kSigCentral) return R::err(bad("ZIP-Verzeichnis ungültig"));
        const u8* h = &cd[p];
        const std::size_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
        if (p + kCentralSize + nameLen + extraLen + commentLen > cd.size()) return R::err(bad("ZIP-Verzeichnis ungültig"));
        ZipEntry e;
        e.name.assign(reinterpret_cast<const char*>(h + kCentralSize), nameLen);
        const u32 flags = le16(h + 8);
        e.method = static_cast<u16>(le16(h + 10));
        e.crc = le32(h + 16);
        e.compSize = le32(h + 20);
        e.size = le32(h + 24);
        e.localOffset = le32(h + 42);
        e.isDir = !e.name.empty() && e.name.back() == '/';
        if (!isSafeZipPath(e.name)) return R::err(bad("unsicherer Pfad: " + e.name));
        if (flags & 1u) return R::err(bad("verschlüsselt: " + e.name));
        if (e.method != 0 && e.method != 8) return R::err(bad("Methode " + std::to_string(e.method) + ": " + e.name));
        if (e.size > kMaxZipEntryBytes || e.compSize > kMaxZipEntryBytes) return R::err(bad("zu groß: " + e.name));
        if (e.method == 0 && e.compSize != e.size) return R::err(bad("Größe ungültig: " + e.name));
        if (e.localOffset >= cdOffset) return R::err(bad("Offset ungültig: " + e.name));
        if (u64{e.localOffset} + kLocalSize + e.compSize > cdOffset) return R::err(bad("Größe ungültig: " + e.name));
        // Deflate schafft höchstens ~1032:1 (258 Byte je 2-Bit-Code); mehr ist gelogen.
        if (e.method == 8 && u64{e.size} > u64{e.compSize} * 1032 + 1024)
            return R::err(bad("Größe ungültig: " + e.name));
        out.push_back(std::move(e));
        p += kCentralSize + nameLen + extraLen + commentLen;
    }
    return R::ok(std::move(out));
}

Result<std::vector<u8>> extractZipEntry(FileSystem& fs, std::string_view zip, const ZipEntry& e) {
    using R = Result<std::vector<u8>>;
    u8 local[kLocalSize];
    if (!readExact(fs, zip, e.localOffset, local) || le32(local) != kSigLocal) return R::err(bad("lokaler Kopf: " + e.name));
    const u64 dataPos = static_cast<u64>(e.localOffset) + kLocalSize + le16(local + 26) + le16(local + 28);
    std::vector<u8> packed(e.compSize);
    if (!readExact(fs, zip, dataPos, packed)) return R::err(bad("Daten unvollständig: " + e.name));

    std::vector<u8> data;
    if (e.method == 8) {
        auto r = inflateRaw(packed, e.size);
        if (!r.isOk()) return R::err(bad("Deflate: " + e.name));
        data = std::move(r).value();
    } else {
        data = std::move(packed);
    }
    if (data.size() != e.size || crc32(data) != e.crc) return R::err(bad("Prüfsumme: " + e.name));
    return R::ok(std::move(data));
}

}  // namespace nl
