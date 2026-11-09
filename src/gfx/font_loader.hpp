#pragma once
// Baut beim Start den UI-Atlas (Spec 3.7): FreeType rendert die eingebetteten Schriften in drei
// Größen und zwei Schnitten; dazu ein weißer Block und ein Kreis für Formen. Nur Wii.

#include "core/result.hpp"
#include "gfx/font.hpp"
#include "gfx/shapes.hpp"

#include <memory>

namespace nl::gfx {

inline constexpr u16 kAtlasWidth = 1024, kAtlasHeight = 512;

struct UiAtlas {
    u8* pixelsI8 = nullptr;  // 32-Byte-ausgerichtet, GX-Kachelformat, kAtlasWidth × kAtlasHeight
    AtlasShapes shapes{};
    FontSet fonts{};

    UiAtlas() = default;
    UiAtlas(const UiAtlas&) = delete;
    UiAtlas& operator=(const UiAtlas&) = delete;
    ~UiAtlas();
};

// Liegt auf dem Heap: FontSet ist ~46 KB groß und gehört nicht auf den Stack.
Result<std::unique_ptr<UiAtlas>> buildUiAtlas();

}  // namespace nl::gfx
