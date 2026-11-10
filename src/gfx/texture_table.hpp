#pragma once
// Texturen außer dem UI-Atlas, wie sie der Renderer sieht: Cover-Texel (CMPR 200×280) je TextureId.

#include "core/types.hpp"
#include "gfx/draw_list.hpp"

namespace nl::gfx {

class CoverTextures {
public:
    virtual const void* texels(TextureId id) const = 0;  // nullptr = nicht (mehr) da, Batch auslassen
    virtual u32 generation() const = 0;                  // ändert sich, wenn Texel kommen oder gehen

protected:
    ~CoverTextures() = default;
};

}  // namespace nl::gfx
