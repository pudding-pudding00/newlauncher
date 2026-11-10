#pragma once
// Big-Endian-Lesen und -Schreiben, feldweise (Spec 5.5). Strings: u16 Länge + UTF-8.
// Der Leser liest nie über das Pufferende; nach dem ersten Fehler bleibt er im Fehlerzustand.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace nl {

class BeWriter {
public:
    void u8(nl::u8 v) { out_.push_back(v); }
    void u16(nl::u16 v);
    void u32(nl::u32 v);
    void u64(nl::u64 v);
    void str(std::string_view s);  // > 65535 Byte → auf eine Codepoint-Grenze gekürzt
    const std::vector<nl::u8>& bytes() const { return out_; }

private:
    std::vector<nl::u8> out_;
};

class BeReader {
public:
    explicit BeReader(std::span<const nl::u8> data) : data_(data) {}
    bool u8(nl::u8& v);
    bool u16(nl::u16& v);
    bool u32(nl::u32& v);
    bool u64(nl::u64& v);
    bool str(std::string& v);
    bool strView(std::string_view& v);  // zeigt in den Puffer, keine Kopie
    bool ok() const { return ok_; }
    std::size_t remaining() const { return ok_ ? data_.size() - pos_ : 0; }

private:
    bool need(std::size_t n);

    std::span<const nl::u8> data_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace nl
