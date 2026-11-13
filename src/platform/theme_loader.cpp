#include "platform/theme_loader.hpp"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include <gccore.h>

#include "core/ini.hpp"
#include "core/rgb5a3.hpp"
#include "platform/png_decode.hpp"

namespace nl::platform {

namespace {
constexpr std::size_t kMaxThemeIniBytes = 64 * 1024;
}  // namespace

void ThemeBackground::reset() {
    std::free(texels_);
    texels_ = nullptr;
    width_ = height_ = 0;
    u1_ = v1_ = 1.0f;
}

bool ThemeBackground::assign(const std::vector<u8>& texels, u16 texW, u16 texH, u32 imageW, u32 imageH) {
    reset();
    if (texels.empty() || texels.size() > kMaxBackgroundTextureBytes || texW == 0 || texH == 0) return false;
    void* p = memalign(32, texels.size());
    if (p == nullptr) return false;
    std::memcpy(p, texels.data(), texels.size());
    DCFlushRange(p, texels.size());
    texels_ = p;
    width_ = texW;
    height_ = texH;
    u1_ = static_cast<float>(imageW) / static_cast<float>(texW);
    v1_ = static_cast<float>(imageH) / static_cast<float>(texH);
    return true;
}

LoadedTheme loadThemePack(FileSystem& fs, std::string_view dataDir, std::string_view pack, ThemeBackground& bg) {
    LoadedTheme out;
    bg.reset();
    if (pack.empty() || !isThemePackName(pack)) return out;
    const std::string dir = joinPath(joinPath(dataDir, "themes"), pack);

    auto ini = fs.readAll(joinPath(dir, "theme.ini"), kMaxThemeIniBytes);
    if (!ini.isOk()) {
        out.bad.push_back("theme.ini");
        return out;
    }
    const auto& text = ini.value();
    out.pair = themeFromIni(IniDoc::parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size())),
                            &out.bad);

    // Hintergrund: fehlt er, ist das kein Fehler. Zu groß oder kaputt → abgelehnt, bevor dekodiert wird.
    auto png = fs.readAll(joinPath(dir, "background.png"), kMaxBackgroundFileBytes + 1);
    if (!png.isOk()) {
        if (png.error().code != ErrorCode::NotFound) out.bad.push_back("background");
        return out;
    }
    const auto size = pngSize(png.value());
    if (png.value().size() > kMaxBackgroundFileBytes || !size || !backgroundFits(size->first, size->second)) {
        out.bad.push_back("background");
        return out;
    }
    u32 w = 0, h = 0;
    auto rgba = decodePng(png.value(), w, h);
    if (!rgba.isOk() || !backgroundFits(w, h)) {
        out.bad.push_back("background");
        return out;
    }
    const Rgb5a3Image img = encodeRgb5a3(rgba.value(), static_cast<u16>(w), static_cast<u16>(h));
    if (!bg.assign(img.texels, img.width, img.height, w, h)) out.bad.push_back("background");
    return out;
}

}  // namespace nl::platform
