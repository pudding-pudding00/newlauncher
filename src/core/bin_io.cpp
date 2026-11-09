#include "core/bin_io.hpp"

namespace nl {

void BeWriter::u16(nl::u16 v) {
    out_.push_back(static_cast<nl::u8>(v >> 8));
    out_.push_back(static_cast<nl::u8>(v));
}

void BeWriter::u32(nl::u32 v) {
    u16(static_cast<nl::u16>(v >> 16));
    u16(static_cast<nl::u16>(v));
}

void BeWriter::u64(nl::u64 v) {
    u32(static_cast<nl::u32>(v >> 32));
    u32(static_cast<nl::u32>(v));
}

void BeWriter::str(std::string_view s) {
    std::size_t n = s.size();
    if (n > 0xFFFF) {
        n = 0xFFFF;
        // Nicht mitten in einer UTF-8-Folge abschneiden: Folgebytes (10xxxxxx) samt Startbyte entfernen.
        while (n > 0 && (static_cast<nl::u8>(s[n]) & 0xC0) == 0x80) --n;
    }
    u16(static_cast<nl::u16>(n));
    out_.insert(out_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

bool BeReader::need(std::size_t n) {
    if (!ok_ || data_.size() - pos_ < n) ok_ = false;
    return ok_;
}

bool BeReader::u8(nl::u8& v) {
    if (!need(1)) return false;
    v = data_[pos_++];
    return true;
}

bool BeReader::u16(nl::u16& v) {
    if (!need(2)) return false;
    v = static_cast<nl::u16>(data_[pos_] << 8 | data_[pos_ + 1]);
    pos_ += 2;
    return true;
}

bool BeReader::u32(nl::u32& v) {
    nl::u16 hi = 0, lo = 0;
    if (!u16(hi) || !u16(lo)) return false;
    v = static_cast<nl::u32>(hi) << 16 | lo;
    return true;
}

bool BeReader::u64(nl::u64& v) {
    nl::u32 hi = 0, lo = 0;
    if (!u32(hi) || !u32(lo)) return false;
    v = static_cast<nl::u64>(hi) << 32 | lo;
    return true;
}

bool BeReader::strView(std::string_view& v) {
    nl::u16 n = 0;
    if (!u16(n) || !need(n)) return false;
    v = std::string_view(reinterpret_cast<const char*>(data_.data() + pos_), n);
    pos_ += n;
    return true;
}

bool BeReader::str(std::string& v) {
    std::string_view s;
    if (!strView(s)) return false;
    v.assign(s);
    return true;
}

}  // namespace nl
