#include "core/meta_db.hpp"

#include "core/bin_io.hpp"

namespace nl {

namespace {
Result<MetaDb> invalid(const char* why) { return Result<MetaDb>::err(Error{ErrorCode::InvalidData, why}); }

bool readEntry(BeReader& r, u16 version, std::string_view& key, MetaEntry& e) {
    if (!(r.strView(key) && r.strView(e.title) && r.u16(e.year) && r.u8(e.players) && r.strView(e.description)))
        return false;
    if (version < 2) return true;
    u8 system = 0;
    if (!(r.u32(e.genres) && r.u8(system) && r.u8(e.rating.age))) return false;
    if (system > static_cast<u8>(RatingSystem::Cero)) e.rating = Rating{};  // unbekanntes System → unbewertet
    else e.rating.system = static_cast<RatingSystem>(system);
    return true;
}
}  // namespace

Result<MetaDb> MetaDb::parse(std::vector<u8> file) {
    MetaDb db;
    db.file_ = std::move(file);
    BeReader r(db.file_);
    u8 magic[4] = {};
    for (u8& b : magic) r.u8(b);
    u16 version = 0, reserved = 0;
    u32 count = 0;
    r.u16(version);
    r.u16(reserved);
    r.u32(count);
    if (!r.ok()) return invalid("meta.bin: Header abgeschnitten");
    if (magic[0] != 'N' || magic[1] != 'L' || magic[2] != 'M' || magic[3] != 'D') return invalid("meta.bin: falsches Magic");
    if (version != 1 && version != 2) return invalid("meta.bin: unbekannte Version");
    db.version_ = version;
    if (count > r.remaining() / 9) return invalid("meta.bin: Anzahl passt nicht zur Dateigröße");

    db.offsets_.reserve(count);
    std::string_view prev;
    for (u32 i = 0; i < count; ++i) {
        const u32 offset = static_cast<u32>(db.file_.size() - r.remaining());
        std::string_view key;
        MetaEntry e{};
        if (!readEntry(r, version, key, e)) return invalid("meta.bin: abgeschnitten");
        if (i > 0 && !(prev < key)) return invalid("meta.bin: Schlüssel nicht sortiert");
        prev = key;
        db.offsets_.push_back(offset);
    }
    if (r.remaining() != 0) return invalid("meta.bin: Daten nach dem Ende");
    return Result<MetaDb>::ok(std::move(db));
}

std::optional<MetaEntry> MetaDb::find(std::string_view key) const {
    std::size_t lo = 0, hi = offsets_.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        BeReader r(std::span<const u8>(file_).subspan(offsets_[mid]));
        std::string_view k;
        MetaEntry e{};
        readEntry(r, version_, k, e);  // bereits in parse geprüft
        if (k == key) return e;
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    return std::nullopt;
}

std::string metaKey(const IndexGame& g) {
    if (g.console == Console::Wii || g.console == Console::Gc) return g.key;
    std::string k = g.key;
    const std::size_t slash = k.find('/');
    const std::size_t dot = k.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) k.resize(dot);
    for (char& c : k)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return k;
}

}  // namespace nl
