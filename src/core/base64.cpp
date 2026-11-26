#include "core/base64.hpp"

namespace nl {

namespace {
constexpr char kStd[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kUrl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int value(char c, bool url) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == (url ? '-' : '+')) return 62;
    if (c == (url ? '_' : '/')) return 63;
    return -1;
}
}  // namespace

std::string base64Encode(std::span<const u8> data, bool url) {
    const char* abc = url ? kUrl : kStd;
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const u32 v = (u32{data[i]} << 16) | (u32{data[i + 1]} << 8) | data[i + 2];
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += abc[(v >> 6) & 63];
        out += abc[v & 63];
    }
    const std::size_t rest = data.size() - i;
    if (rest > 0) {
        u32 v = u32{data[i]} << 16;
        if (rest == 2) v |= u32{data[i + 1]} << 8;
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        if (rest == 2) out += abc[(v >> 6) & 63];
        if (!url) out.append(rest == 1 ? "==" : "=");
    }
    return out;
}

std::optional<std::vector<u8>> base64Decode(std::string_view text, bool url) {
    std::size_t pad = 0;
    if (!url) {
        if (text.size() % 4 != 0) return std::nullopt;
        while (pad < 2 && pad < text.size() && text[text.size() - 1 - pad] == '=') ++pad;
        text.remove_suffix(pad);
    } else if (text.size() % 4 == 1) {
        return std::nullopt;
    }
    std::vector<u8> out;
    out.reserve(text.size() * 3 / 4);
    u32 acc = 0;
    int bits = 0;
    for (const char c : text) {
        const int v = value(c, url);
        if (v < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<u32>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<u8>(acc >> bits));
            acc &= (1u << bits) - 1;
        }
    }
    if (acc != 0) return std::nullopt;  // nicht kanonische Restbits
    if (!url && pad > 0 && (text.size() + pad) % 4 != 0) return std::nullopt;
    return out;
}

}  // namespace nl
