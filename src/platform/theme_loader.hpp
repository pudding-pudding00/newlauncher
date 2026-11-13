#pragma once
// Theme-Paket laden (Spec v2 10.2): theme.ini und optional background.png aus <data>/themes/<name>.
// Ein kaputtes Paket verhindert nie den Start: kaputte Tokens nehmen den Standard, ein unpassendes
// Bild entfällt. Jede Ablehnung steht in bad ("light.bar", "background").

#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"
#include "core/theme_file.hpp"

namespace nl::platform {

// Hintergrund als GX-Textur RGB5A3, 32-Byte-ausgerichtet; höchstens kMaxBackgroundTextureBytes.
class ThemeBackground {
public:
    ThemeBackground() = default;
    ThemeBackground(const ThemeBackground&) = delete;
    ThemeBackground& operator=(const ThemeBackground&) = delete;
    ~ThemeBackground() { reset(); }

    void reset();
    // Übernimmt fertige Texel (Größe auf Vielfache von 4 aufgefüllt); imageW/H ist der sichtbare Teil.
    bool assign(const std::vector<u8>& texels, u16 texW, u16 texH, u32 imageW, u32 imageH);

    const void* texels() const { return texels_; }
    u16 width() const { return width_; }
    u16 height() const { return height_; }
    float u1() const { return u1_; }  // sichtbarer Bildteil in Texturkoordinaten
    float v1() const { return v1_; }

private:
    void* texels_ = nullptr;
    u16 width_ = 0, height_ = 0;
    float u1_ = 1.0f, v1_ = 1.0f;
};

struct LoadedTheme {
    ThemePair pair;
    std::vector<std::string> bad;
};

// pack leer → Standardfarben, kein Bild. Fehlt theme.ini, gilt das Paket als Standard und bad = {"theme.ini"}.
LoadedTheme loadThemePack(FileSystem& fs, std::string_view dataDir, std::string_view pack, ThemeBackground& bg);

}  // namespace nl::platform
