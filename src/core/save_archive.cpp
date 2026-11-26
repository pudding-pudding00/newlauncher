#include "core/save_archive.hpp"

#include <algorithm>
#include <charconv>

#include "core/ini.hpp"
#include "core/sha256.hpp"
#include "core/zip_reader.hpp"
#include "core/zip_writer.hpp"

namespace nl {
namespace {

using Bytes = std::vector<u8>;

Error invalid(std::string what) { return Error{ErrorCode::InvalidData, "Spielstand-Paket: " + std::move(what)}; }

// Freitext (Titel, Gerät) darf die INI-Zeile nicht verlassen: Steuerzeichen werden zu Leerzeichen.
std::string oneLine(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
    return out;
}

template <typename T>
bool parseNum(std::string_view s, T& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

std::string metaText(const SaveMeta& m) {
    IniDoc d;
    d.set("save", "format", std::to_string(m.format));
    d.set("save", "unit", oneLine(m.unit));
    d.set("save", "title", oneLine(m.title));
    d.set("save", "device", oneLine(m.device));
    d.set("save", "saved", std::to_string(m.saved));
    d.set("save", "hash", m.hash);
    d.set("save", "files", std::to_string(m.files));
    return d.serialize();
}

Result<SaveMeta> parseMeta(std::span<const u8> raw) {
    using R = Result<SaveMeta>;
    const IniDoc d = IniDoc::parse(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    SaveMeta m;
    if (!parseNum(d.get("save", "format"), m.format)) return R::err(invalid("format fehlt"));
    if (m.format != kSaveFormat)
        return R::err(Error{ErrorCode::VersionMismatch, "Spielstand-Paket: Format " + std::to_string(m.format)});
    m.unit = std::string(d.get("save", "unit"));
    m.title = std::string(d.get("save", "title"));
    m.device = std::string(d.get("save", "device"));
    m.hash = std::string(d.get("save", "hash"));
    if (!parseNum(d.get("save", "saved"), m.saved)) return R::err(invalid("saved fehlt"));
    if (!parseNum(d.get("save", "files"), m.files)) return R::err(invalid("files fehlt"));
    if (m.unit.empty() || m.hash.size() != 64) return R::err(invalid("unit/hash fehlt"));
    return R::ok(std::move(m));
}

struct Opened {
    std::vector<ZipEntry> entries;
    SaveMeta meta;
};

Result<Opened> open(std::span<const u8> zip) {
    using R = Result<Opened>;
    auto dir = readZipDirectory(zip);
    if (!dir.isOk()) return R::err(dir.error());
    std::vector<ZipEntry> entries = std::move(dir.value());
    if (entries.empty() || entries[0].name != kSaveMetaName) return R::err(invalid("Meta fehlt oder steht nicht vorn"));
    auto raw = extractZipEntry(zip, entries[0]);
    if (!raw.isOk()) return R::err(raw.error());
    auto meta = parseMeta(raw.value());
    if (!meta.isOk()) return R::err(meta.error());
    return R::ok(Opened{std::move(entries), std::move(meta.value())});
}

}  // namespace

std::string contentHash(std::span<const SaveFile> files) {
    std::vector<const SaveFile*> sorted;
    sorted.reserve(files.size());
    for (const SaveFile& f : files) sorted.push_back(&f);
    std::sort(sorted.begin(), sorted.end(), [](const SaveFile* a, const SaveFile* b) { return a->path < b->path; });
    Sha256 h;
    for (const SaveFile* f : sorted) {
        const std::string line = f->path + "\n" + std::to_string(f->data.size()) + "\n" + bytesToHex(sha256(f->data)) + "\n";
        h.update(std::span<const u8>(reinterpret_cast<const u8*>(line.data()), line.size()));
    }
    return bytesToHex(h.finish());
}

Result<Bytes> packSave(SaveMeta meta, std::span<const SaveFile> files) {
    using R = Result<Bytes>;
    std::size_t total = 0;
    for (const SaveFile& f : files) {
        total += f.data.size();
        if (total > kMaxSaveUnitBytes) return R::err(invalid("größer als 16 MiB"));
    }
    meta.format = kSaveFormat;
    meta.hash = contentHash(files);
    meta.files = static_cast<u32>(files.size());
    const u32 dos = dosTimeFromUnix(meta.saved);
    const std::string text = metaText(meta);
    ZipWriter w;
    if (auto r = w.add(kSaveMetaName, std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()), false, dos);
        !r.isOk())
        return R::err(r.error());
    for (const SaveFile& f : files) {
        if (f.path == kSaveMetaName) return R::err(invalid("reservierter Name"));
        if (auto r = w.add(f.path, f.data, true, dos); !r.isOk()) return R::err(r.error());
    }
    return R::ok(w.finish());
}

Result<SavePackage> unpackSave(std::span<const u8> zip, std::size_t maxRaw) {
    using R = Result<SavePackage>;
    auto o = open(zip);
    if (!o.isOk()) return R::err(o.error());
    Opened& op = o.value();
    std::size_t total = 0;
    for (std::size_t i = 1; i < op.entries.size(); ++i) {
        total += op.entries[i].size;
        if (total > maxRaw) return R::err(invalid("größer als erlaubt"));
    }
    SavePackage pkg;
    pkg.meta = std::move(op.meta);
    for (std::size_t i = 1; i < op.entries.size(); ++i) {
        const ZipEntry& e = op.entries[i];
        if (e.isDir) continue;
        auto data = extractZipEntry(zip, e);
        if (!data.isOk()) return R::err(data.error());
        pkg.files.push_back(SaveFile{e.name, std::move(data.value())});
    }
    // ZipWriter lehnt doppelte Namen ab, fremde Archive nicht: doppelte Pfade machen den Hash mehrdeutig.
    std::vector<std::string_view> names;
    for (const SaveFile& f : pkg.files) names.push_back(f.path);
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) return R::err(invalid("doppelter Pfad"));
    if (pkg.files.size() != pkg.meta.files) return R::err(invalid("Dateizahl stimmt nicht"));
    if (contentHash(pkg.files) != pkg.meta.hash) return R::err(invalid("Inhalts-Hash stimmt nicht"));
    return R::ok(std::move(pkg));
}

Result<SaveMeta> readSaveMeta(std::span<const u8> zip) {
    using R = Result<SaveMeta>;
    auto o = open(zip);
    if (!o.isOk()) return R::err(o.error());
    return R::ok(std::move(o.value().meta));
}

}  // namespace nl
