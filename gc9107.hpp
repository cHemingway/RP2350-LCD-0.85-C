// Driver for GC9107 128x128 RGB565 display (Waveshare RP2350-LCD-0.85)
#pragma once

#include <cstddef>
#include <cstdint>
#include "hardware/spi.h"

namespace rgb565 {
constexpr uint16_t from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t RED    = 0xF800;
constexpr uint16_t GREEN  = 0x07E0;
constexpr uint16_t BLUE   = 0x001F;
constexpr uint16_t WHITE  = 0xFFFF;
constexpr uint16_t BLACK  = 0x0000;
constexpr uint16_t YELLOW = 0xFFE0;
} // namespace rgb565

class GC9107 {
public:
    static constexpr uint16_t WIDTH  = 128;
    static constexpr uint16_t HEIGHT = 128;

    struct Pins {
        uint cs, rst, dc, sck, mosi;
    };

    // Configures the pins, SPI bus and a DMA channel, then resets and initialises the panel.
    // frame_rate_hz is the panel refresh rate (25-123 Hz, rounded to the nearest supported).
    GC9107(spi_inst_t *spi, const Pins &pins, uint baudrate = 75'000'000, uint frame_rate_hz = 90);
    ~GC9107();
    GC9107(const GC9107 &) = delete;
    GC9107 &operator=(const GC9107 &) = delete;

    // All drawing calls are asynchronous (DMA). Each one first waits for the previous
    // transfer, so they can be chained freely; call wait() before touching a pixel
    // buffer that is still being sent.
    void fill(uint16_t color) { fill_rect(0, 0, WIDTH, HEIGHT, color); }
    void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels);
    void show(const uint16_t *framebuffer) { draw(0, 0, WIDTH, HEIGHT, framebuffer); }

    // ready() is the non-blocking form of wait(): it returns true (and releases the bus)
    // once the current transfer has finished.
    bool ready();
    uint baudrate() const { return baudrate_; }     // actual SPI clock achieved
    uint frame_rate() const { return frame_rate_; } // actual panel refresh rate (Hz)
    void wait();

private:
    static constexpr uint8_t X_OFFSET = 0;
    static constexpr uint8_t Y_OFFSET = 32;

    void reset();
    void init_panel(uint frame_rate_hz);
    void command(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0);
    void command(uint8_t cmd, uint8_t data) { command(cmd, &data, 1); }
    bool set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void start_pixels(const uint16_t *src, uint32_t count, bool increment);

    spi_inst_t *spi_;
    Pins pins_;
    uint dma_chan_;
    uint16_t fill_color_ = 0; // DMA source for fill_rect, must outlive the transfer
    bool active_ = false;
    uint baudrate_;
    uint frame_rate_;
};
