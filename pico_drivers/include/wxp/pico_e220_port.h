// E220Port on a Pico: UART with an interrupt-driven receive ring buffer, plus
// the M0/M1/AUX pins and the module's power switch.
#pragma once

#include "hardware/uart.h"
#include "wx/e220.h"

namespace wxp {

struct E220Pins {
  unsigned uart_index;  // 0 or 1
  uint32_t baud;
  unsigned tx, rx, m0, m1, aux, power_en;
};

class PicoE220Port : public wx::E220Port {
 public:
  explicit PicoE220Port(const E220Pins& pins) : pins_(pins) {}

  void init();

  void set_mode_pins(bool m0, bool m1) override;
  bool aux_high() override;
  void set_power(bool on) override;
  void write(const uint8_t* data, size_t n) override;
  int read_byte() override;

  uint32_t rx_overflows() const;

 private:
  E220Pins pins_;
  uart_inst_t* uart_ = nullptr;
};

}  // namespace wxp
