#include "plasma.hpp"

#include <cmath>
#include "gc9107.hpp" // rgb565::from_rgb

Plasma::Plasma()
{
    constexpr float TAU = 6.2831853f;
    for (int i = 0; i < 256; i++) {
        const float a = TAU * i / 256;
        sine_[i] = uint8_t(128 + 127 * sinf(a));
        // Red, green and blue a third of a cycle apart: a rainbow that loops back on itself
        palette_[i] = rgb565::from_rgb(uint8_t(128 + 127 * sinf(a)), uint8_t(128 + 127 * sinf(a + TAU / 3)),
                                       uint8_t(128 + 127 * sinf(a + 2 * TAU / 3)));
    }
}

void Plasma::draw(uint16_t *fb, int width, int height, float t) const
{
    if (width > MAX_WIDTH)
        return;

    // Each wave's phase advances at its own rate; uint8_t wraps round the 256-entry sine table.
    // (Go via int: converting a float outside 0..255 straight to uint8_t is undefined.)
    auto phase = [t](float rate) { return uint8_t(int(t * rate)); };
    const uint8_t p1 = phase(60), p2 = phase(-45), p3 = phase(35), p4 = phase(-80);
    const uint8_t shift = phase(50); // palette cycling

    // Centre of the circular ripples wanders around the screen
    const float cx = width * (0.5f + 0.3f * sinf(t * 0.7f));
    const float cy = height * (0.5f + 0.3f * cosf(t * 0.9f));

    // The horizontal wave only depends on x, so compute it once per column
    uint8_t column[MAX_WIDTH];
    for (int x = 0; x < width; x++)
        column[x] = sine_[uint8_t(x * 3 + p1)];

    for (int y = 0; y < height; y++) {
        const uint8_t row = sine_[uint8_t(y * 2 + p2)];
        const float dy2 = (y - cy) * (y - cy);
        for (int x = 0; x < width; x++) {
            const float dist = sqrtf((x - cx) * (x - cx) + dy2); // hardware VSQRT on the M33
            const unsigned sum = column[x] + row + sine_[uint8_t((x + y) * 2 + p3)] +
                                 sine_[uint8_t(int(dist * 5) + p4)];
            *fb++ = palette_[uint8_t(sum / 4 + shift)];
        }
    }
}
