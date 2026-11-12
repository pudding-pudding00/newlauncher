#include "ui/launch_text.hpp"

#include <cstring>
#include <string>

#include "ui/strings.hpp"

namespace nl::ui {

namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

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

int launchErrorText(const LaunchError& e, char* title, std::size_t titleCap, char* text, std::size_t textCap) {
    copyEllipsized(title, titleCap, tr(Str::LaunchFailed));
    std::string msg;
    switch (e.kind) {
        case LaunchFailure::EmuMissing: msg = fill(tr(Str::EmuMissing), e.context); break;
        case LaunchFailure::EmuInvalid: msg = fill(tr(Str::EmuInvalid), e.context); break;
        case LaunchFailure::RomMissing: msg = fill(tr(Str::RomMissing), e.context); break;
        case LaunchFailure::ArgsTooLong: msg = tr(Str::ArgsTooLong); break;
        case LaunchFailure::Unsupported: msg = fill(tr(Str::Unsupported), e.context); break;
        case LaunchFailure::NoMemory: msg = e.context; break;
        case LaunchFailure::NoN64Path: msg = tr(Str::NoN64Path); break;
        case LaunchFailure::NinMissing: msg = fill(tr(Str::NinMissing), e.context); break;
        case LaunchFailure::NinUnknown: msg = fill(tr(Str::NinUnknown), e.context); break;
        case LaunchFailure::NinTooOld: msg = fill(tr(Str::NinTooOld), e.context); break;
        case LaunchFailure::NinPathTooLong: msg = fill(tr(Str::NinPathTooLong), e.context); break;
        case LaunchFailure::AhbProt: msg = tr(Str::AhbProt); break;
        case LaunchFailure::NinFormat: msg = fill(tr(Str::NinFormat), e.context); break;
        case LaunchFailure::CiosMissing: msg = tr(Str::CiosMissing); break;
        case LaunchFailure::CiosNotD2x: msg = fill(tr(Str::CiosNotD2x), e.context); break;
        case LaunchFailure::WiiFsUnsupported: msg = fill(tr(Str::WiiFsUnsupported), e.context); break;
        case LaunchFailure::WiiFileBroken: msg = fill(tr(Str::WiiFileBroken), e.context); break;
    }
    return static_cast<int>(copyEllipsized(text, textCap, msg));
}

}  // namespace nl::ui
