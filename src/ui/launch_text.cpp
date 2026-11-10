#include "ui/launch_text.hpp"

#include <cstring>
#include <string>

#include "ui/strings.hpp"

namespace nl::ui {

namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

bool endsWithDol(std::string_view s) {
    if (s.size() < 4) return false;
    const std::string_view ext = s.substr(s.size() - 4);
    constexpr std::string_view want = ".dol";
    for (std::size_t i = 0; i < 4; ++i) {
        char c = ext[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != want[i]) return false;
    }
    return true;
}

// Ersetzt das erste %s im Muster durch arg (Pfade dürfen selbst % enthalten, daher kein printf).
std::string fill(std::string_view pattern, std::string_view arg) {
    std::string out(pattern);
    const std::size_t at = out.find("%s");
    if (at != std::string::npos) out.replace(at, 2, arg);
    return out;
}

}  // namespace

std::size_t copyEllipsized(char* out, std::size_t cap, std::string_view src) {
    if (cap == 0) return 0;
    if (src.size() < cap) {
        std::memcpy(out, src.data(), src.size());
        out[src.size()] = '\0';
        return src.size();
    }
    if (cap <= kEllipsis.size()) {
        out[0] = '\0';
        return 0;
    }
    std::size_t cut = cap - 1 - kEllipsis.size();
    while (cut > 0 && (static_cast<unsigned char>(src[cut]) & 0xC0) == 0x80) --cut;
    std::memcpy(out, src.data(), cut);
    std::memcpy(out + cut, kEllipsis.data(), kEllipsis.size());
    out[cut + kEllipsis.size()] = '\0';
    return cut + kEllipsis.size();
}

int launchErrorText(const Error& e, char* title, std::size_t titleCap, char* text, std::size_t textCap) {
    copyEllipsized(title, titleCap, kStrLaunchFailed);
    std::string msg;
    switch (e.code) {
        case ErrorCode::NotFound:
            msg = fill(endsWithDol(e.context) ? kStrEmuMissing : kStrRomMissing, e.context);
            break;
        case ErrorCode::InvalidData:
            msg = endsWithDol(e.context) ? fill(kStrEmuInvalid, e.context) : std::string(kStrArgsTooLong);
            break;
        case ErrorCode::Unsupported:
            msg = fill(kStrUnsupported, e.context);
            break;
        default:
            msg = e.context;
            break;
    }
    return static_cast<int>(copyEllipsized(text, textCap, msg));
}

}  // namespace nl::ui
