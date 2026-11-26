#include "core/url_encode.hpp"

namespace nl {

void appendPercentEncoded(std::string& out, std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '.' || c == '_' || c == '~';
        if (plain) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
}

std::string percentEncode(std::string_view s) {
    std::string out;
    appendPercentEncoded(out, s);
    return out;
}

std::string formEncode(std::span<const std::pair<std::string_view, std::string_view>> pairs) {
    std::string out;
    for (const auto& [k, v] : pairs) {
        if (!out.empty()) out += '&';
        appendPercentEncoded(out, k);
        out += '=';
        appendPercentEncoded(out, v);
    }
    return out;
}

}  // namespace nl
