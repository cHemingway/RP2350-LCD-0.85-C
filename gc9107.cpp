// Driver for GC9107 display
#include "gc9107.hpp"

#include <initializer_list>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/time.h"

namespace {
namespace cmd {
constexpr uint8_t SLPOUT = 0x11;
constexpr uint8_t DISPON = 0x29;
constexpr uint8_t CASET  = 0x2A;
constexpr uint8_t RASET  = 0x2B;
constexpr uint8_t RAMWR  = 0x2C;
constexpr uint8_t COLMOD = 0x3A;
constexpr uint8_t FRSET  = 0xA8; // needs bit 0 of 0xB6 set (done in INIT_REGS)
} // namespace cmd

// Frame Rate Set (A8h) table, datasheet p.126: frame period ~= 8130 us + 252.5 us * RTN1,
// RTN1 = 0..127 (123 Hz .. 25 Hz).
constexpr uint8_t frame_rate_to_rtn(uint hz)
{
    const int rtn = hz ? ((int(1'000'000 / hz) - 8130) * 2 + 252) / 505 : 127;
    return rtn < 0 ? 0 : rtn > 127 ? 127 : uint8_t(rtn);
}
constexpr uint rtn_to_frame_rate(uint8_t rtn) { return 2'000'000 / (16260 + 505 * rtn); }
static_assert(frame_rate_to_rtn(90) == 0x0C); // Waveshare's original setting

// Vendor register setup, as in Waveshare's reference driver: {register, value}
constexpr uint8_t INIT_REGS[][2] = {
    {0xB0, 0xC0}, {0xB2, 0x24}, {0xB3, 0x03}, {0xB7, 0x01}, {0xB6, 0x19},
    {0xAC, 0xDB}, {0xAB, 0x0F}, {cmd::COLMOD, 0x05}, // 16-bit RGB565
    {0xB4, 0x04}, {0xB8, 0x08},
    {0xED, 0x03}, {0xEA, 0x9F}, {0xC6, 0x2A}, {0xC7, 0x10},
};
} // namespace

GC9107::GC9107(spi_inst_t *spi, const Pins &pins, uint baudrate, uint frame_rate_hz)
    : spi_(spi), pins_(pins), dma_chan_(dma_claim_unused_channel(true))
{
    for (uint pin : {pins_.cs, pins_.rst, pins_.dc}) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 1);
    }
    baudrate_ = spi_init(spi_, baudrate);
    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    for (uint pin : {pins_.sck, pins_.mosi}) {
        gpio_set_function(pin, GPIO_FUNC_SPI);
        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST); // default slow slew rounds off edges at 75 MHz
        // gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_8MA); // only if still unreliable
    }

    reset();
    init_panel(frame_rate_hz);
}

GC9107::~GC9107()
{
    wait();
    dma_channel_unclaim(dma_chan_);
}

void GC9107::reset()
{
    gpio_put(pins_.rst, 0);
    sleep_ms(100);
    gpio_put(pins_.rst, 1);
    sleep_ms(100);
}

void GC9107::init_panel(uint frame_rate_hz)
{
    command(cmd::SLPOUT);
    sleep_ms(120);
    command(cmd::DISPON);
    sleep_ms(120);

    command(0xFE); // inter-register enable 1
    command(0xFE);
    command(0xEF); // inter-register enable 2

    for (const auto &[reg, value] : INIT_REGS)
        command(reg, value);

    const uint8_t rtn = frame_rate_to_rtn(frame_rate_hz);
    command(cmd::FRSET, rtn);
    frame_rate_ = rtn_to_frame_rate(rtn);
}

void GC9107::command(uint8_t c, const uint8_t *data, size_t len)
{
    wait();
    gpio_put(pins_.cs, 0);
    gpio_put(pins_.dc, 0);
    spi_write_blocking(spi_, &c, 1); // returns once the byte has fully shifted out
    if (len) {
        gpio_put(pins_.dc, 1);
        spi_write_blocking(spi_, data, len);
    }
    gpio_put(pins_.cs, 1);
}

bool GC9107::set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0 || x + w > WIDTH || y + h > HEIGHT)
        return false;

    const uint8_t cols[] = {0, uint8_t(x + X_OFFSET), 0, uint8_t(x + w - 1 + X_OFFSET)};
    const uint8_t rows[] = {0, uint8_t(y + Y_OFFSET), 0, uint8_t(y + h - 1 + Y_OFFSET)};
    command(cmd::CASET, cols, sizeof cols);
    command(cmd::RASET, rows, sizeof rows);
    command(cmd::RAMWR);
    return true;
}

void GC9107::start_pixels(const uint16_t *src, uint32_t count, bool increment)
{
    // 16-bit SPI frames send each pixel MSB first, so native uint16_t RGB565 needs no byte swap
    spi_set_format(spi_, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(pins_.cs, 0);
    gpio_put(pins_.dc, 1);

    dma_channel_config c = dma_channel_get_default_config(dma_chan_);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(spi_, true));
    channel_config_set_read_increment(&c, increment);
    channel_config_set_write_increment(&c, false);
    active_ = true;
    dma_channel_configure(dma_chan_, &c, &spi_get_hw(spi_)->dr, src, count, true);
}

void GC9107::fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!set_window(x, y, w, h))
        return;
    fill_color_ = color; // safe: set_window() has waited for any previous transfer
    start_pixels(&fill_color_, uint32_t(w) * h, false);
}

void GC9107::draw(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels)
{
    if (set_window(x, y, w, h))
        start_pixels(pixels, uint32_t(w) * h, true);
}

bool GC9107::ready()
{
    if (!active_)
        return true;
    // DMA done != last frame shifted out, so check the SPI as well
    if (dma_channel_is_busy(dma_chan_) || spi_is_busy(spi_))
        return false;

    gpio_put(pins_.cs, 1);

    // TX-only DMA leaves junk (and an overrun flag) in the RX FIFO; clear it
    while (spi_is_readable(spi_))
        (void)spi_get_hw(spi_)->dr;
    spi_get_hw(spi_)->icr = SPI_SSPICR_RORIC_BITS;

    spi_set_format(spi_, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    active_ = false;
    return true;
}

void GC9107::wait()
{
    while (!ready())
        tight_loop_contents();
}
