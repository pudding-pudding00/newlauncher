#include "platform/png_decode.hpp"

#include <csetjmp>
#include <cstring>

#include <png.h>

namespace nl::platform {

namespace {
struct Reader {
    std::span<const u8> data;
    std::size_t pos = 0;
};

void readFn(png_structp png, png_bytep out, png_size_t n) {
    auto* r = static_cast<Reader*>(png_get_io_ptr(png));
    if (n > r->data.size() - r->pos) png_error(png, "PNG abgeschnitten");
    std::memcpy(out, r->data.data() + r->pos, n);
    r->pos += n;
}

void errorFn(png_structp png, png_const_charp) { png_longjmp(png, 1); }
void warnFn(png_structp, png_const_charp) {}

Error fail(const char* what) { return Error{ErrorCode::InvalidData, what}; }
}  // namespace

Result<std::vector<u8>> decodePng(std::span<const u8> file, u32& width, u32& height) {
    width = height = 0;
    if (file.size() < 8 || png_sig_cmp(file.data(), 0, 8) != 0)
        return Result<std::vector<u8>>::err(fail("keine PNG-Signatur"));

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, errorFn, warnFn);
    if (png == nullptr) return Result<std::vector<u8>>::err(fail("png_create_read_struct"));
    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return Result<std::vector<u8>>::err(fail("png_create_info_struct"));
    }

    Reader reader{file, 0};
    // Alles, was nach setjmp verändert und nach einem longjmp gelesen wird, liegt außerhalb des Stacks
    // dieses Frames oder ist volatile.
    std::vector<u8> pixels;
    std::vector<png_bytep> rows;
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_read_struct(&png, &info, nullptr);
        return Result<std::vector<u8>>::err(fail("PNG beschädigt"));
    }
    png_set_read_fn(png, &reader, readFn);
    png_set_user_limits(png, kMaxPngSide, kMaxPngSide);
    png_read_info(png, info);

    const png_uint_32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
    if (w == 0 || h == 0 || static_cast<u64>(w) * h > kMaxPngPixels) {
        png_destroy_read_struct(&png, &info, nullptr);
        return Result<std::vector<u8>>::err(fail("PNG zu groß"));
    }
    const int type = png_get_color_type(png, info);
    if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
    if (type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (type == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png, info) < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != static_cast<png_size_t>(w) * 4) {
        png_destroy_read_struct(&png, &info, nullptr);
        return Result<std::vector<u8>>::err(fail("PNG-Format nicht umwandelbar"));
    }

    pixels.resize(static_cast<std::size_t>(w) * h * 4);
    rows.resize(h);
    for (png_uint_32 y = 0; y < h; ++y) rows[y] = pixels.data() + static_cast<std::size_t>(y) * w * 4;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    width = w;
    height = h;
    return Result<std::vector<u8>>::ok(std::move(pixels));
}

}  // namespace nl::platform
