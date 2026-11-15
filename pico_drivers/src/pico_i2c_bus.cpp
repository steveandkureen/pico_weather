#include "wxp/pico_i2c_bus.h"

#include "hardware/gpio.h"
#include "pico/time.h"

namespace wxp {

void PicoI2cBus::init() {
  i2c_init(i2c_, hz_);
  gpio_set_function(sda_, GPIO_FUNC_I2C);
  gpio_set_function(scl_, GPIO_FUNC_I2C);
  gpio_pull_up(sda_);  // modules have their own pull-ups; these are a backstop
  gpio_pull_up(scl_);
}

uint32_t PicoI2cBus::timeout_us(size_t bytes) const {
  // ~9 clocks per byte plus address, with 3× margin and a 2 ms floor.
  const uint32_t per_byte_us = 9u * 1000000u / hz_;
  uint32_t t = static_cast<uint32_t>(bytes + 2) * per_byte_us * 3u;
  return t < 2000u ? 2000u : t;
}

bool PicoI2cBus::write(uint8_t addr, const uint8_t* data, size_t n) {
  int r = i2c_write_timeout_us(i2c_, addr, data, n, false, timeout_us(n));
  return r == static_cast<int>(n);
}

bool PicoI2cBus::read_reg(uint8_t addr, uint8_t reg, uint8_t* data, size_t n) {
  int r = i2c_write_timeout_us(i2c_, addr, &reg, 1, true, timeout_us(1));
  if (r != 1) return false;
  r = i2c_read_timeout_us(i2c_, addr, data, n, false, timeout_us(n));
  return r == static_cast<int>(n);
}

bool PicoI2cBus::sda_high() const { return gpio_get(sda_); }
bool PicoI2cBus::scl_high() const { return gpio_get(scl_); }

int PicoI2cBus::scan(uint8_t* found, int cap) {
  int n = 0;
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    uint8_t b;
    if (i2c_read_timeout_us(i2c_, a, &b, 1, false, 2000) == 1 && n < cap) found[n++] = a;
  }
  return n;
}

void PicoI2cBus::recover() {
  ++recoveries_;
  // Take the pins over as GPIO and clock SCL until a stuck device releases
  // SDA (at most 9 clocks finishes any byte it was sending), then a STOP.
  i2c_deinit(i2c_);
  gpio_set_function(sda_, GPIO_FUNC_SIO);
  gpio_set_function(scl_, GPIO_FUNC_SIO);
  gpio_set_dir(sda_, GPIO_IN);
  gpio_pull_up(sda_);
  gpio_put(scl_, true);
  gpio_set_dir(scl_, GPIO_OUT);
  for (int i = 0; i < 9 && !gpio_get(sda_); ++i) {
    gpio_put(scl_, false);
    busy_wait_us(10);
    gpio_put(scl_, true);
    busy_wait_us(10);
  }
  // STOP: SDA low → high while SCL high.
  gpio_put(sda_, false);
  gpio_set_dir(sda_, GPIO_OUT);
  busy_wait_us(10);
  gpio_put(scl_, true);
  busy_wait_us(10);
  gpio_put(sda_, true);
  busy_wait_us(10);
  gpio_set_dir(sda_, GPIO_IN);
  init();
}

}  // namespace wxp
