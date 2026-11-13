#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

// I2C bus as the sensor drivers see it. The Pico implementation puts a short
// deadline on every transfer; a fake implements it in the host tests.
class I2cBus {
 public:
  virtual ~I2cBus() = default;
  // Write `n` bytes. Returns false on NAK or timeout.
  virtual bool write(uint8_t addr, const uint8_t* data, size_t n) = 0;
  // Write register address, then read `n` bytes (repeated start).
  virtual bool read_reg(uint8_t addr, uint8_t reg, uint8_t* data, size_t n) = 0;
  // Unstick the bus: clock out a device holding SDA low, then re-init.
  virtual void recover() = 0;
};

}  // namespace wx
