#include "core/zip_writer.hpp"

#include <algorithm>

#include "core/civil_time.hpp"
#include "core/crc32.hpp"
#include "core/deflate.hpp"
#include "core/zip_reader.hpp"

namespace nl {
namespace {

constexpr u32 kSigLocal = 0x04034b50u, kSigCentral = 0x02014b50u, kSigEnd = 0x06054b50u;
constexpr u16 kVersion = 20;     // 2.0: Deflate
constexpr u16 kFlagUtf8 = 1u << 11;

void put16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x));
    v.push_back(static_cast<u8>(x >> 8));
}
void put32(std::vector<u8>& v, u32 x) {
    put16(v, x & 0xFFFFu);
    put16(v, x >> 16);
}

}  // namespace

u32 dosTimeFromUnix(u64 unix) {
    const CivilTime t = civilFromUnix(unix);
    if (t.year < 1980) return (u32{(1u << 5) | 1u}) << 16;
    const u32 date = (static_cast<u32>(t.year - 1980) << 9) | (t.month << 5) | t.day;
    const u32 time = (t.hour << 11) | (t.minute << 5) | (t.second / 2);
    return (date << 16) | time;
}

Result<void> ZipWriter::add(std::string_view name, std::span<const u8> data, bool deflate, u32 dosTime) {
    using R = Result<void>;
    if (!isSafeZipPath(name) || name.back() == '/') return R::err(Error{ErrorCode::InvalidData, "unsicherer Name: " + std::string(name)});
    if (std::find(names_.begin(), names_.end(), name) != names_.end())
        return R::err(Error{ErrorCode::InvalidData, "doppelter Name: " + std::string(name)});
    if (names_.size() >= kMaxZipEntries) return R::err(Error{ErrorCode::InvalidData, "zu viele Einträge"});
    if (data.size() > kMaxZipEntryBytes) return R::err(Error{ErrorCode::InvalidData, "zu groß: " + std::string(name)});

    const u32 crc = crc32(data);
    std::vector<u8> packed;
    u16 method = 0;
    if (deflate && !data.empty()) {
        packed = deflateRaw(data);
        if (packed.size() < data.size()) method = 8;
    }
    const std::span<const u8> body = method == 8 ? std::span<const u8>(packed) : data;
    const u32 offset = static_cast<u32>(out_.size());

    put32(out_, kSigLocal);
    put16(out_, kVersion);
    put16(out_, kFlagUtf8);
    put16(out_, method);
    put32(out_, dosTime);
    put32(out_, crc);
    put32(out_, static_cast<u32>(body.size()));
    put32(out_, static_cast<u32>(data.size()));
    put16(out_, static_cast<u32>(name.size()));
    put16(out_, 0);
    out_.insert(out_.end(), name.begin(), name.end());
    out_.insert(out_.end(), body.begin(), body.end());

    put32(central_, kSigCentral);
    put16(central_, kVersion);  // erstellt mit
    put16(central_, kVersion);  // nötig zum Entpacken
    put16(central_, kFlagUtf8);
    put16(central_, method);
    put32(central_, dosTime);
    put32(central_, crc);
    put32(central_, static_cast<u32>(body.size()));
    put32(central_, static_cast<u32>(data.size()));
    put16(central_, static_cast<u32>(name.size()));
    put16(central_, 0);  // extra
    put16(central_, 0);  // Kommentar
    put16(central_, 0);  // Datenträger
    put16(central_, 0);  // interne Attribute
    put32(central_, 0);  // externe Attribute
    put32(central_, offset);
    central_.insert(central_.end(), name.begin(), name.end());
    names_.emplace_back(name);
    return R::ok();
}

std::vector<u8> ZipWriter::finish() {
    const u32 cdOffset = static_cast<u32>(out_.size());
    out_.insert(out_.end(), central_.begin(), central_.end());
    put32(out_, kSigEnd);
    put16(out_, 0);
    put16(out_, 0);
    put16(out_, static_cast<u32>(names_.size()));
    put16(out_, static_cast<u32>(names_.size()));
    put32(out_, static_cast<u32>(central_.size()));
    put32(out_, cdOffset);
    put16(out_, 0);
    central_.clear();
    names_.clear();
    return std::move(out_);
}

}  // namespace nl
