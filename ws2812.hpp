// Single WS2812B LED, driven by the Pico SDK's ws2812 PIO program
#pragma once

#include <cstdint>
#include "hardware/pio.h"

class WS2812 {
public:
    // brightness scales every colour: 255 = full, 26 = ~10%
    explicit WS2812(uint pin, uint8_t brightness = 255);
    ~WS2812();
    WS2812(const WS2812 &) = delete;
    WS2812 &operator=(const WS2812 &) = delete;

    void set(uint8_t r, uint8_t g, uint8_t b);
    void set_rgb565(uint16_t color);
    void off() { set(0, 0, 0); }

private:
    PIO pio_;
    uint sm_, offset_;
    uint8_t brightness_;
};
