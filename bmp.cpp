#include "bmp.hpp"

#include <cstring>
#include "gc9107.hpp" // rgb565::from_rgb

namespace {
uint32_t read_le(const uint8_t *p, size_t bytes)
{
    uint32_t v = 0;
    for (size_t i = 0; i < bytes; i++)
        v |= uint32_t(p[i]) << (8 * i);
    return v;
}

constexpr uint32_t BI_RGB = 0, BI_BITFIELDS = 3;

struct Header {
    const uint8_t *pixels; // first stored row
    uint32_t width, height, row_size, bpp;
    bool top_down;
};

std::optional<Header> parse(const uint8_t *bmp, size_t size)
{
    if (size < 54 || bmp[0] != 'B' || bmp[1] != 'M')
        return std::nullopt;

    const uint32_t start = read_le(bmp + 10, 4);
    const uint32_t width = read_le(bmp + 18, 4);
    const int32_t raw_height = int32_t(read_le(bmp + 22, 4)); // negative = stored top-down
    const uint32_t height = raw_height < 0 ? -raw_height : raw_height;
    const uint32_t bpp = read_le(bmp + 28, 2);
    const uint32_t compression = read_le(bmp + 30, 4);

    if (bpp == 24 && compression != BI_RGB)
        return std::nullopt;
    if (bpp == 16) {
        // 16-bit without bitfields means RGB555, so insist on explicit RGB565 masks
        // (at offset 54 for both BITMAPINFOHEADER + masks and V4/V5 headers)
        if (compression != BI_BITFIELDS || size < 66 || read_le(bmp + 54, 4) != 0xF800 ||
            read_le(bmp + 58, 4) != 0x07E0 || read_le(bmp + 62, 4) != 0x001F)
            return std::nullopt;
    } else if (bpp != 24) {
        return std::nullopt;
    }

    const uint32_t row_size = (width * (bpp / 8) + 3) & ~3u;
    if (start + size_t(row_size) * height > size)
        return std::nullopt;
    return Header{bmp + start, width, height, row_size, bpp, raw_height < 0};
}
} // namespace

std::optional<BmpSize> decode_bmp(const uint8_t *bmp, size_t size, uint16_t *out, size_t out_pixels)
{
    const auto h = parse(bmp, size);
    if (!h || size_t(h->width) * h->height > out_pixels)
        return std::nullopt;

    for (uint32_t y = 0; y < h->height; y++) {
        const uint8_t *px = h->pixels + (h->top_down ? y : h->height - 1 - y) * h->row_size;
        if (h->bpp == 16) {
            std::memcpy(out, px, h->width * 2); // already little-endian RGB565
            out += h->width;
        } else {
            for (uint32_t x = 0; x < h->width; x++, px += 3)
                *out++ = rgb565::from_rgb(px[2], px[1], px[0]); // stored as B, G, R
        }
    }
    return BmpSize{h->width, h->height};
}

std::optional<BmpView> bmp_rgb565_view(const uint8_t *bmp, size_t size)
{
    const auto h = parse(bmp, size);
    if (!h || h->bpp != 16 || !h->top_down || h->row_size != h->width * 2 ||
        reinterpret_cast<uintptr_t>(h->pixels) % 2)
        return std::nullopt;
    return BmpView{reinterpret_cast<const uint16_t *>(h->pixels), h->width, h->height};
}
