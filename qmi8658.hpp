// Driver for the QMI8658A 6-axis IMU (accelerometer + gyroscope) over I2C, with optional
// tap detection on INT1. Register details from the QMI8658A datasheet Rev A.
#pragma once

#include <cstddef>
#include <cstdint>
#include "hardware/i2c.h"

class QMI8658 {
public:
    enum class AccelRange : uint8_t { G2, G4, G8, G16 };
    enum class GyroRange : uint8_t { DPS16, DPS32, DPS64, DPS128, DPS256, DPS512, DPS1024, DPS2048 };
    // Nominal output data rates (register codes); applies to both sensors
    enum class Odr : uint8_t { Hz1000 = 3, Hz500, Hz250, Hz125, Hz62_5, Hz31_25 };

    // Tap engine parameters (datasheet §10.2). Windows are in samples at the configured ODR;
    // the defaults are the datasheet's example values for 500 Hz.
    struct TapConfig {
        uint8_t peak_window = 20;       // max duration of a peak
        uint8_t priority = 0;           // axis priority when peaks coincide: 0 = X > Y > Z
        uint16_t tap_window = 50;       // quiet time required after a tap
        uint16_t dtap_window = 250;     // max time for the second tap of a double tap
        uint8_t alpha = 8;              // /128: weight of new data in the acceleration average
        uint8_t gamma = 32;             // /128: weight of new data in the movement average
        uint16_t peak_mag_thr = 0x0320; // /1024 g²: peak detection threshold
        uint16_t udm_thr = 0x0190;      // /1024 g: "quiet" threshold
    };

    struct Config {
        AccelRange accel_range = AccelRange::G4;
        GyroRange gyro_range = GyroRange::DPS512;
        Odr odr = Odr::Hz125;   // the datasheet recommends > 200 Hz when using tap detection
        bool tap_enabled = false; // detect taps and signal them on INT1 (active high)
        TapConfig tap;
    };

    enum class Tap : uint8_t { None, Single, Double };

    struct Sample {
        float ax, ay, az; // g
        float gx, gy, gz; // degrees/s
        float temp_c;     // die temperature
    };

    struct Pins {
        uint sda, scl;
    };

    static constexpr uint8_t DEFAULT_ADDR = 0x6B; // SA0 high (as on the RP2350-LCD-0.85)

    // Sets up the I2C bus; call begin() to detect and configure the sensor.
    QMI8658(i2c_inst_t *i2c, const Pins &pins, uint8_t addr = DEFAULT_ADDR, uint baudrate = 400'000);

    // Returns false if the sensor doesn't respond with the right WHO_AM_I.
    bool begin(const Config &config);
    bool begin() { return begin(Config{}); }

    // Averages `samples` gyro readings (board must be still) and subtracts that offset from
    // future reads. Returns false on I2C error or timeout.
    bool calibrate_gyro(uint samples = 100);

    bool data_ready();        // true when both sensors have a new sample
    bool read(Sample &out);   // false on I2C error

    // Returns the tap detected since the last call (if any). Call when INT1 rises.
    Tap read_tap();

private:
    bool write_reg(uint8_t reg, uint8_t value);
    bool write_regs(uint8_t reg, const uint8_t *data, size_t len);
    bool ctrl9_command(uint8_t cmd);
    bool configure_tap(const TapConfig &tap);
    bool read_regs(uint8_t reg, uint8_t *buf, size_t len);

    i2c_inst_t *i2c_;
    uint8_t addr_;
    float accel_scale_ = 0; // g per LSB
    float gyro_scale_ = 0;  // dps per LSB
    float gyro_bias_[3] = {}; // dps
};
