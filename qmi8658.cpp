#include "qmi8658.hpp"

#include <initializer_list>

#include "hardware/gpio.h"
#include "pico/time.h"

namespace {
namespace reg {
constexpr uint8_t WHO_AM_I = 0x00;
constexpr uint8_t CTRL1    = 0x02; // serial interface
constexpr uint8_t CTRL2    = 0x03; // accel range / ODR
constexpr uint8_t CTRL3    = 0x04; // gyro range / ODR
constexpr uint8_t CTRL7    = 0x08; // sensor enables
constexpr uint8_t CTRL8    = 0x09; // motion detection control
constexpr uint8_t CTRL9    = 0x0A; // host command
constexpr uint8_t CAL1_L   = 0x0B; // CAL1_L..CAL4_H: CTRL9 command parameters
constexpr uint8_t STATUSINT = 0x2D;
constexpr uint8_t STATUS0  = 0x2E; // data available
constexpr uint8_t STATUS1  = 0x2F; // motion events
constexpr uint8_t TEMP_L   = 0x33; // TEMP_L..H, then AX_L..GZ_H
constexpr uint8_t TAP_STATUS = 0x59;
} // namespace reg

constexpr uint8_t WHO_AM_I_VALUE = 0x05;
constexpr uint8_t CTRL1_ADDR_AI  = 0x40; // auto-increment address (and BE=0: little-endian)
constexpr uint8_t CTRL1_INT1_EN  = 0x08;
constexpr uint8_t CTRL7_AEN      = 0x01;
constexpr uint8_t CTRL7_GEN      = 0x02;
constexpr uint8_t CTRL8_HANDSHAKE_STATUSINT = 0x80; // CTRL9 handshake via STATUSINT.7, not INT1
constexpr uint8_t CTRL8_ACTIVITY_INT1 = 0x40;       // motion events (incl. tap) on INT1
constexpr uint8_t CTRL8_TAP_EN   = 0x01;
constexpr uint8_t CTRL9_ACK      = 0x00;
constexpr uint8_t CTRL9_CONFIGURE_TAP = 0x0C;
constexpr uint8_t STATUSINT_CMD_DONE = 0x80;
constexpr uint8_t STATUS0_ADA    = 0x01;
constexpr uint8_t STATUS0_GDA    = 0x02;
constexpr uint8_t STATUS1_TAP    = 0x02;
constexpr uint32_t TIMEOUT_US    = 1000;

int16_t le16(const uint8_t *p) { return int16_t(p[0] | p[1] << 8); }
} // namespace

QMI8658::QMI8658(i2c_inst_t *i2c, const Pins &pins, uint8_t addr, uint baudrate) : i2c_(i2c), addr_(addr)
{
    i2c_init(i2c_, baudrate);
    for (uint pin : {pins.sda, pins.scl}) {
        gpio_set_function(pin, GPIO_FUNC_I2C);
        gpio_pull_up(pin); // weak; the board should have external pull-ups too
    }
}

bool QMI8658::begin(const Config &config)
{
    uint8_t id;
    if (!read_regs(reg::WHO_AM_I, &id, 1) || id != WHO_AM_I_VALUE)
        return false;

    const uint8_t odr = uint8_t(config.odr);
    const uint8_t afs = uint8_t(config.accel_range);
    const uint8_t gfs = uint8_t(config.gyro_range);

    // Sensors must be disabled while configuring (they may still be on after a warm reboot)
    if (!write_reg(reg::CTRL7, 0) ||
        !write_reg(reg::CTRL1, CTRL1_ADDR_AI | (config.tap_enabled ? CTRL1_INT1_EN : 0)) ||
        !write_reg(reg::CTRL2, afs << 4 | odr) || !write_reg(reg::CTRL3, gfs << 4 | odr) ||
        !write_reg(reg::CTRL8, CTRL8_HANDSHAKE_STATUSINT))
        return false;
    if (config.tap_enabled &&
        (!configure_tap(config.tap) ||
         !write_reg(reg::CTRL8, CTRL8_HANDSHAKE_STATUSINT | CTRL8_ACTIVITY_INT1 | CTRL8_TAP_EN)))
        return false;
    if (!write_reg(reg::CTRL7, CTRL7_AEN | CTRL7_GEN))
        return false;

    accel_scale_ = float(2 << afs) / 32768;  // ±2g << afs
    gyro_scale_ = float(16 << gfs) / 32768;  // ±16dps << gfs
    sleep_ms(100);                            // let the gyro start up (as vendor driver)
    return true;
}

bool QMI8658::data_ready()
{
    uint8_t status;
    return read_regs(reg::STATUS0, &status, 1) && (status & (STATUS0_ADA | STATUS0_GDA)) == (STATUS0_ADA | STATUS0_GDA);
}

bool QMI8658::read(Sample &out)
{
    uint8_t b[14];
    if (!read_regs(reg::TEMP_L, b, sizeof b))
        return false;
    out.temp_c = le16(b) / 256.0f;
    out.ax = le16(b + 2) * accel_scale_;
    out.ay = le16(b + 4) * accel_scale_;
    out.az = le16(b + 6) * accel_scale_;
    out.gx = le16(b + 8) * gyro_scale_ - gyro_bias_[0];
    out.gy = le16(b + 10) * gyro_scale_ - gyro_bias_[1];
    out.gz = le16(b + 12) * gyro_scale_ - gyro_bias_[2];
    return true;
}

bool QMI8658::calibrate_gyro(uint samples)
{
    float sum[3] = {};
    gyro_bias_[0] = gyro_bias_[1] = gyro_bias_[2] = 0;
    for (uint i = 0; i < samples; i++) {
        const absolute_time_t timeout = make_timeout_time_ms(100);
        while (!data_ready()) {
            if (time_reached(timeout))
                return false;
        }
        Sample s;
        if (!read(s))
            return false;
        sum[0] += s.gx;
        sum[1] += s.gy;
        sum[2] += s.gz;
    }
    for (int i = 0; i < 3; i++)
        gyro_bias_[i] = sum[i] / samples;
    return true;
}

QMI8658::Tap QMI8658::read_tap()
{
    uint8_t status1, tap_status;
    if (!read_regs(reg::STATUS1, &status1, 1) || !(status1 & STATUS1_TAP) ||
        !read_regs(reg::TAP_STATUS, &tap_status, 1))
        return Tap::None;
    switch (tap_status & 0x03) { // TAP_NUM
    case 1: return Tap::Single;
    case 2: return Tap::Double;
    default: return Tap::None;
    }
}

// Tap parameters are passed in two CTRL9 commands (datasheet §10.3, Table 36)
bool QMI8658::configure_tap(const TapConfig &t)
{
    const uint8_t set1[] = {t.peak_window, t.priority, uint8_t(t.tap_window), uint8_t(t.tap_window >> 8),
                            uint8_t(t.dtap_window), uint8_t(t.dtap_window >> 8), 0, 0x01};
    const uint8_t set2[] = {t.alpha, t.gamma, uint8_t(t.peak_mag_thr), uint8_t(t.peak_mag_thr >> 8),
                            uint8_t(t.udm_thr), uint8_t(t.udm_thr >> 8), 0, 0x02};
    return write_regs(reg::CAL1_L, set1, sizeof set1) && ctrl9_command(CTRL9_CONFIGURE_TAP) &&
           write_regs(reg::CAL1_L, set2, sizeof set2) && ctrl9_command(CTRL9_CONFIGURE_TAP);
}

// CTRL9 protocol (datasheet §5.10.4): write the command, wait for CmdDone, then acknowledge
bool QMI8658::ctrl9_command(uint8_t cmd)
{
    if (!write_reg(reg::CTRL9, cmd))
        return false;
    const absolute_time_t timeout = make_timeout_time_ms(100);
    uint8_t status = 0;
    while (!(status & STATUSINT_CMD_DONE)) {
        if (time_reached(timeout) || !read_regs(reg::STATUSINT, &status, 1))
            return false;
    }
    return write_reg(reg::CTRL9, CTRL9_ACK);
}

bool QMI8658::write_reg(uint8_t reg, uint8_t value)
{
    return write_regs(reg, &value, 1);
}

bool QMI8658::write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[9];
    if (len + 1 > sizeof buf)
        return false;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++)
        buf[i + 1] = data[i];
    return i2c_write_timeout_us(i2c_, addr_, buf, len + 1, false, TIMEOUT_US * (len + 1)) == int(len + 1);
}

bool QMI8658::read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_timeout_us(i2c_, addr_, &reg, 1, true, TIMEOUT_US) == 1 &&
           i2c_read_timeout_us(i2c_, addr_, buf, len, false, TIMEOUT_US * len) == int(len);
}
