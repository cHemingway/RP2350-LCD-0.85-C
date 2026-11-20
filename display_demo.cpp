
#include <stdio.h>
#include <array>
#include <cstdint>

#include "pico/stdlib.h"
#include "gc9107.hpp"
#include "bmp.hpp"
#include "ws2812.hpp"

// Embedded by CMake (embed_binary)
extern "C" const uint8_t flash_img_bmp[], flash_img_bmp_end[];

static uint16_t framebuffer[GC9107::WIDTH * GC9107::HEIGHT];


constexpr uint GPIO_CS = 13;
constexpr uint GPIO_RST = 10;
constexpr uint GPIO_DC = 12;
constexpr uint GPIO_SCK = 14;
constexpr uint GPIO_MOSI = 15;

constexpr uint GPIO_WS2812B = 25;

template <typename... Pins>
constexpr uint32_t pin_mask(Pins... pins) { return ((1u << pins) | ... | 0u); }

int main()
{
    stdio_init_all();

    using namespace rgb565;
    std::array<uint16_t, 5> colors = {BLACK, RED, GREEN, BLUE, WHITE};
    uint current_color = 0;

    GC9107 lcd(spi1, {.cs = GPIO_CS, .rst = GPIO_RST, .dc = GPIO_DC, .sck = GPIO_SCK, .mosi = GPIO_MOSI});
    printf("Started, SPI at %u Hz\n", lcd.baudrate());
    WS2812 led(GPIO_WS2812B, 12); // ~5% brightness


    while (true) {
        const absolute_time_t t0 = get_absolute_time();
        const size_t bmp_size = flash_img_bmp_end - flash_img_bmp;
        // Top-down RGB565 BMPs can be sent straight from flash; anything else is decoded to RAM
        auto view = bmp_rgb565_view(flash_img_bmp, bmp_size);
        auto img = view ? std::nullopt : decode_bmp(flash_img_bmp, bmp_size, framebuffer, std::size(framebuffer));
        const absolute_time_t t1 = get_absolute_time();
        if (view)
            lcd.draw(0, 0, view->width, view->height, view->pixels);
        else if (img)
            lcd.draw(0, 0, img->width, img->height, framebuffer);
        else
            lcd.fill(RED); // decode failed
        lcd.wait();
        const absolute_time_t t2 = get_absolute_time();
        led.off();

        printf("bmp: decode %lld us, display %lld us, total %lld us\n",
               absolute_time_diff_us(t0, t1), absolute_time_diff_us(t1, t2), absolute_time_diff_us(t0, t2));
        sleep_ms(5000);

        // Now show RGB
        for (uint16_t color: colors) {
            lcd.fill(color);
            led.set_rgb565(color);
            lcd.wait();
            sleep_ms(1000);
        }
    }
}
