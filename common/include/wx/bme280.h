// Bosch BME280 temperature / humidity / pressure sensor.
//
// Runs in "forced mode": each reading wakes the sensor, measures once (~10 ms)
// and it goes back to sleep. That avoids self-heating, which matters outdoors.
//
// The driver is a non-blocking state machine: poll() does at most one short
// I2C transfer and returns. Failures reset the bus and re-initialize the chip
// with growing back-off; the last good reading is marked invalid after
// repeated failures so stale values are never reported as current.
#pragma once

#include <cstdint>

#include "wx/i2c_bus.h"

namespace wx {

struct Bme280Calib {
  uint16_t t1;
  int16_t t2, t3;
  uint16_t p1;
  int16_t p2, p3, p4, p5, p6, p7, p8, p9;
  uint8_t h1;
  int16_t h2;
  uint8_t h3;
  int16_t h4, h5;
  int8_t h6;
};

// Datasheet compensation formulas (integer versions).
// Returns temperature in °C×100 and sets t_fine for the other two.
int32_t bme280_temp_x100(const Bme280Calib& c, int32_t adc_t, int32_t* t_fine);
// Pressure in Pa (0 on invalid input).
uint32_t bme280_pressure_pa(const Bme280Calib& c, int32_t adc_p, int32_t t_fine);
// Relative humidity in %×100.
uint32_t bme280_humidity_x100(const Bme280Calib& c, int32_t adc_h, int32_t t_fine);

// Parses the two calibration blocks: 26 bytes from 0x88 and 7 bytes from 0xE1.
void bme280_parse_calib(const uint8_t* b88, const uint8_t* be1, Bme280Calib* c);

class Bme280 {
 public:
  static constexpr uint8_t kChipIdBme280 = 0x60;
  static constexpr uint8_t kChipIdBmp280 = 0x58;  // no humidity: common counterfeit/mix-up

  Bme280(I2cBus& bus, uint8_t addr, uint32_t interval_ms) : bus_(bus), addr_(addr), interval_ms_(interval_ms) {}

  // Call often. Never blocks beyond one I2C transfer.
  void poll(uint32_t now_ms);

  // Ask for a fresh reading as soon as possible (outdoor: right before sending).
  void request_now() { next_ms_valid_ = false; }

  struct Reading {
    bool valid;            // false until the first good reading, or after repeated failures
    bool has_humidity;     // false on a BMP280
    int16_t temp_c_x100;
    uint16_t humidity_x100;
    uint32_t pressure_pa;
    uint32_t at_ms;        // when it was taken
  };
  const Reading& reading() const { return r_; }

  uint8_t chip_id() const { return chip_id_; }
  uint8_t addr() const { return addr_; }  // the address that answered (0x76 or 0x77)
  uint32_t errors() const { return errors_; }      // total failed transfers
  // Step that failed last: "probe", "calib", "configure", "start", "read", "range" or "".
  const char* last_fail_step() const { return last_fail_step_; }
  uint32_t progress() const { return progress_; }  // bumps on every completed step (watchdog)

 private:
  enum class State : uint8_t { kProbe, kResetWait, kCalib, kConfigure, kIdle, kMeasuring, kBackoff };

  void fail(uint32_t now_ms, const char* step);
  bool write_reg(uint8_t reg, uint8_t val);

  I2cBus& bus_;
  uint8_t addr_;
  uint32_t interval_ms_;
  State state_ = State::kProbe;
  uint32_t wait_until_ms_ = 0;
  uint32_t next_ms_ = 0;
  bool next_ms_valid_ = false;
  uint8_t chip_id_ = 0;
  Bme280Calib calib_{};
  Reading r_{};
  uint32_t errors_ = 0;
  uint32_t consecutive_fail_ = 0;
  const char* last_fail_step_ = "";
  uint32_t progress_ = 0;
};

}  // namespace wx
