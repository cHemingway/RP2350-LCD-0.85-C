
#include <stdio.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pico/stdlib.h"
#include "gc9107.hpp"
#include "bmp.hpp"
#include "ws2812.hpp"
#include "qmi8658.hpp"
#include "cube.hpp"
#include "starfield.hpp"
#include "plasma.hpp"

// Embedded by CMake (embed_binary)
extern "C" const uint8_t flash_img_bmp[], flash_img_bmp_end[];

static uint16_t framebuffer[GC9107::WIDTH * GC9107::HEIGHT];


constexpr uint GPIO_CS = 13;
constexpr uint GPIO_RST = 10;
constexpr uint GPIO_DC = 12;
constexpr uint GPIO_SCK = 14;
constexpr uint GPIO_MOSI = 15;

constexpr uint GPIO_WS2812B = 25;

constexpr uint GPIO_IMU_SDA = 20;
constexpr uint GPIO_IMU_SCL = 21;
constexpr uint GPIO_IMU_INT1 = 22; // tap interrupt
constexpr uint GPIO_IMU_INT2 = 23; // not used

template <typename... Pins>
constexpr uint32_t pin_mask(Pins... pins) { return ((1u << pins) | ... | 0u); }

// Set from the INT1 interrupt; the tap itself is confirmed over I2C outside the IRQ
static volatile bool imu_int1_flag = false;

static void imu_int1_irq(uint gpio, uint32_t events)
{
    if (gpio == GPIO_IMU_INT1 && (events & GPIO_IRQ_EDGE_RISE))
        imu_int1_flag = true;
}

// True if the board has been tapped since the last call
static bool tapped(QMI8658 &imu)
{
    if (!imu_int1_flag)
        return false;
    imu_int1_flag = false;
    const QMI8658::Tap tap = imu.read_tap();
    if (tap != QMI8658::Tap::None)
        printf("tap: %s\n", tap == QMI8658::Tap::Double ? "double" : "single");
    return tap != QMI8658::Tap::None;
}

// Like sleep_ms(), but returns true early if the board is tapped
static bool sleep_unless_tapped(QMI8658 &imu, uint32_t ms)
{
    const absolute_time_t end = make_timeout_time_ms(ms);
    while (!time_reached(end)) {
        if (tapped(imu))
            return true;
        sleep_ms(1);
    }
    return false;
}

// Runs an animation at the panel's refresh rate (faster frames would never be seen), drawing
// into two framebuffers in turn so one is rendered while the other is sent by DMA. Prints the
// frame rate once a second.
class FrameLoop {
public:
    static constexpr int W = GC9107::WIDTH, H = GC9107::HEIGHT;

    FrameLoop(GC9107 &lcd, const char *name)
        : lcd_(lcd), name_(name), period_us_(1'000'000 / lcd.frame_rate()), next_(get_absolute_time()),
          fps_start_(next_)
    {
    }
    ~FrameLoop() { lcd_.wait(); }

    // Waits until it's time for the next frame, then returns the framebuffer to draw it in
    uint16_t *begin_frame()
    {
        sleep_until(next_);
        next_ = delayed_by_us(next_, period_us_);
        if (time_reached(next_)) // fell behind: don't try to catch up with a burst of frames
            next_ = make_timeout_time_us(period_us_);
        return buffers_[frames_ & 1]; // the other buffer may still be in flight
    }

    void end_frame(uint16_t *fb)
    {
        lcd_.show(fb); // waits for the previous frame's transfer before starting this one
        frames_++;
        const absolute_time_t now = get_absolute_time();
        const int64_t elapsed = absolute_time_diff_us(fps_start_, now);
        if (elapsed >= 1'000'000) {
            printf("%s: %.1f fps\n", name_, (frames_ - fps_frames_) * 1e6f / elapsed);
            fps_frames_ = frames_;
            fps_start_ = now;
        }
    }

private:
    static inline uint16_t back_buffer_[W * H];
    uint16_t *const buffers_[2] = {framebuffer, back_buffer_};
    GC9107 &lcd_;
    const char *name_;
    const int64_t period_us_;
    absolute_time_t next_, fps_start_;
    uint frames_ = 0, fps_frames_ = 0;
};

static absolute_time_t end_time(uint32_t duration_ms)
{
    return duration_ms ? make_timeout_time_ms(duration_ms) : at_the_end_of_time;
}

// Wireframe cube that stays fixed in space as the board is turned, in front of a starfield.
// Runs for duration_ms (0 = forever); returns true if it ended because of a tap.
static bool cube_demo(GC9107 &lcd, QMI8658 &imu, uint32_t duration_ms)
{
    constexpr float DEG = float(M_PI / 180), ALPHA = 0.98f; // complementary filter: trust gyro short-term
    static Starfield stars;
    FrameLoop loop(lcd, "cube");

    float roll = 0, pitch = 0, yaw = 0;
    bool first = true;
    absolute_time_t last = get_absolute_time();
    const absolute_time_t end = end_time(duration_ms);

    while (!time_reached(end)) {
        uint16_t *fb = loop.begin_frame();
        if (tapped(imu))
            return true;

        const absolute_time_t now = get_absolute_time();
        const float dt = absolute_time_diff_us(last, now) * 1e-6f;
        last = now;

        QMI8658::Sample s;
        if (imu.read(s)) {
            // Tilt from gravity (noisy but doesn't drift); yaw has no reference so it's gyro only
            const float acc_roll = atan2f(s.ay, s.az);
            const float acc_pitch = atan2f(-s.ax, sqrtf(s.ay * s.ay + s.az * s.az));
            if (first) {
                roll = acc_roll;
                pitch = acc_pitch;
                first = false;
            }
            roll = ALPHA * (roll + s.gx * DEG * dt) + (1 - ALPHA) * acc_roll;
            pitch = ALPHA * (pitch + s.gy * DEG * dt) + (1 - ALPHA) * acc_pitch;
            yaw += s.gz * DEG * dt;
        }
        stars.update(dt);

        std::fill_n(fb, FrameLoop::W * FrameLoop::H, rgb565::BLACK);
        const View view = View::from_orientation(roll, pitch, yaw);
        stars.draw(fb, FrameLoop::W, FrameLoop::H, view);
        draw_wireframe_cube(fb, FrameLoop::W, FrameLoop::H, view);
        loop.end_frame(fb);
    }
    return false;
}

// Plasma effect; the LED follows the colour at the centre of the screen.
// Runs for duration_ms (0 = forever); returns true if it ended because of a tap.
static bool plasma_demo(GC9107 &lcd, QMI8658 &imu, WS2812 &led, uint32_t duration_ms)
{
    constexpr int W = FrameLoop::W, H = FrameLoop::H;
    static Plasma plasma;
    FrameLoop loop(lcd, "plasma");
    const absolute_time_t start = get_absolute_time(), end = end_time(duration_ms);

    while (!time_reached(end)) {
        uint16_t *fb = loop.begin_frame();
        if (tapped(imu)) {
            led.off();
            return true;
        }
        plasma.draw(fb, W, H, absolute_time_diff_us(start, get_absolute_time()) * 1e-6f);
        led.set_rgb565(fb[(H / 2) * W + W / 2]);
        loop.end_frame(fb);
    }
    led.off();
    return false;
}

int main()
{
    stdio_init_all();

    using namespace rgb565;
    std::array<uint16_t, 5> colors = {BLACK, RED, GREEN, BLUE, WHITE};
    uint current_color = 0;

    GC9107 lcd(spi1, {.cs = GPIO_CS, .rst = GPIO_RST, .dc = GPIO_DC, .sck = GPIO_SCK, .mosi = GPIO_MOSI});
    printf("Started, SPI at %u Hz\n", lcd.baudrate());
    WS2812 led(GPIO_WS2812B, 12); // ~5% brightness

    QMI8658 imu(i2c0, {.sda = GPIO_IMU_SDA, .scl = GPIO_IMU_SCL});
    // 500 Hz: tap detection wants > 200 Hz, and the default tap parameters assume 500 Hz
    QMI8658::Config imu_config{.odr = QMI8658::Odr::Hz500, .tap_enabled = true};
    imu_config.tap.peak_mag_thr /= 2; // ~2x more sensitive than the datasheet example (0.39 g²)
    bool imu_ok = imu.begin(imu_config);
    printf("IMU %s\n", imu_ok ? "found" : "NOT FOUND");
    if (imu_ok) {
        imu_ok = imu.calibrate_gyro(); // ~0.2 s at 500 Hz; keep the board still during boot
        printf("gyro calibration %s\n", imu_ok ? "done" : "FAILED");
    }
    gpio_init(GPIO_IMU_INT1);
    gpio_pull_down(GPIO_IMU_INT1); // INT1 is high-Z until the IMU enables it
    gpio_set_irq_enabled_with_callback(GPIO_IMU_INT1, GPIO_IRQ_EDGE_RISE, true, imu_int1_irq);

    // Tap to switch to the cube; tap again to go back to the slideshow
    bool cube_mode = false;


    while (true) {
        if (cube_mode && imu_ok) {
            cube_demo(lcd, imu, 0); // until tapped
            cube_mode = false;
            continue;
        }

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
        if (sleep_unless_tapped(imu, 2500)) {
            cube_mode = true;
            continue;
        }

        // Now show RGB
        for (uint16_t color: colors) {
            lcd.fill(color);
            led.set_rgb565(color);
            lcd.wait();

            QMI8658::Sample s;
            if (imu_ok && imu.read(s))
                printf("accel %+.2f %+.2f %+.2f g  gyro %+7.1f %+7.1f %+7.1f dps  %.1f C\n",
                       s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.temp_c);
            if (sleep_unless_tapped(imu, 500)) {
                cube_mode = true;
                break;
            }
        }
        led.off();

        // Then the IMU-driven cube; a tap here keeps it running
        if (!cube_mode && imu_ok)
            cube_mode = cube_demo(lcd, imu, 10'000);

        // And the plasma; a tap here switches to the cube
        if (!cube_mode)
            cube_mode = plasma_demo(lcd, imu, led, 10'000);
    }
}
