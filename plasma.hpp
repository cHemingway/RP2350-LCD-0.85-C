// Classic demoscene plasma: a sum of moving sine waves mapped through a cycling colour palette
#pragma once

#include <cstdint>

class Plasma {
public:
    Plasma(); // builds the sine and palette tables

    // Renders the plasma at time t (seconds) into a width x height RGB565 framebuffer
    // (width up to MAX_WIDTH).
    void draw(uint16_t *fb, int width, int height, float t) const;

    static constexpr int MAX_WIDTH = 256;

private:
    uint8_t sine_[256];     // 128 + 127 sin(2πi/256)
    uint16_t palette_[256]; // smooth RGB loop, so index wrap-around is seamless
};
