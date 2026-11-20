// Minimal decoder for uncompressed 24-bit (BGR888) and 16-bit (RGB565 bitfields) BMP images
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

struct BmpSize {
    uint32_t width, height;
};

// Decodes a BMP into top-down RGB565 pixels in `out`, which must hold at least
// `out_pixels` entries. Returns the image size, or nullopt if the data isn't a
// supported BMP or doesn't fit in `out`.
std::optional<BmpSize> decode_bmp(const uint8_t *bmp, size_t size, uint16_t *out, size_t out_pixels);

struct BmpView {
    const uint16_t *pixels; // points into the BMP data itself
    uint32_t width, height;
};

// Zero-copy access to a BMP whose pixel data is already exactly what the display wants:
// RGB565, top-down (negative height), even width (no row padding) and 2-byte aligned.
// Returns nullopt otherwise; fall back to decode_bmp() in that case.
std::optional<BmpView> bmp_rgb565_view(const uint8_t *bmp, size_t size);
