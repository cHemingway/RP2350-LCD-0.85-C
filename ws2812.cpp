#include "ws2812.hpp"

#include "hardware/clocks.h"
#include "ws2812.pio.h" // generated from the SDK's pico_status_led/ws2812.pio

WS2812::WS2812(uint pin, uint8_t brightness) : brightness_(brightness)
{
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio_, &sm_, &offset_, pin, 1, true));
    ws2812_program_init(pio_, sm_, offset_, pin, 800'000, false);
}

WS2812::~WS2812()
{
    pio_remove_program_and_unclaim_sm(&ws2812_program, pio_, sm_, offset_);
}

void WS2812::set(uint8_t r, uint8_t g, uint8_t b)
{
    auto scale = [this](uint8_t c) { return uint32_t(c * brightness_ / 255); };
    // WS2812B takes GRB, MSB first; the PIO shifts out the top 24 bits
    pio_sm_put_blocking(pio_, sm_, scale(g) << 24 | scale(r) << 16 | scale(b) << 8);
}

void WS2812::set_rgb565(uint16_t c)
{
    set(((c >> 11) & 0x1F) * 255 / 31, ((c >> 5) & 0x3F) * 255 / 63, (c & 0x1F) * 255 / 31);
}
