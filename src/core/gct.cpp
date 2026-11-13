#include "core/gct.hpp"

namespace nl {
namespace {

void putBe32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v >> 24));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v));
}

}  // namespace

std::vector<u8> buildGct(std::span<const GeckoCode* const> codes) {
    std::vector<u8> out;
    putBe32(out, 0x00D0C0DE);
    putBe32(out, 0x00D0C0DE);
    for (const GeckoCode* c : codes) {
        for (u32 w : c->words) putBe32(out, w);
    }
    putBe32(out, 0xF0000000);
    putBe32(out, 0x00000000);
    return out;
}

}  // namespace nl
