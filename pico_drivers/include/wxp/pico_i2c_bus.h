// I2cBus on the RP2350's hardware I2C, with a deadline on every transfer and
// a bus-recovery routine for a sensor that's holding SDA low.
#pragma once

#include "hardware/i2c.h"
#include "wx/i2c_bus.h"

namespace wxp {

class PicoI2cBus : public wx::I2cBus {
 public:
  PicoI2cBus(i2c_inst_t* i2c, unsigned sda, unsigned scl, uint32_t hz)
      : i2c_(i2c), sda_(sda), scl_(scl), hz_(hz) {}

  void init();
  bool write(uint8_t addr, const uint8_t* data, size_t n) override;
  bool read_reg(uint8_t addr, uint8_t reg, uint8_t* data, size_t n) override;
  void recover() override;

  uint32_t recoveries() const { return recoveries_; }

 private:
  uint32_t timeout_us(size_t bytes) const;

  i2c_inst_t* i2c_;
  unsigned sda_, scl_;
  uint32_t hz_;
  uint32_t recoveries_ = 0;
};

}  // namespace wxp
