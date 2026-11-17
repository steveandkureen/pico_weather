// RadioPort on a Pico: UART with an interrupt-driven receive ring buffer, plus
// the module's AUX, M0/M1 and power-switch pins.
#pragma once

#include "hardware/uart.h"
#include "wx/dxlr22.h"

namespace wxp {

struct RadioPins {
  unsigned uart_index;  // 0 or 1
  uint32_t baud;
  unsigned tx, rx, m0, m1, aux, power_en;
};

class PicoRadioPort : public wx::RadioPort {
 public:
  explicit PicoRadioPort(const RadioPins& pins) : pins_(pins) {}

  void init();

  bool aux_busy() override;
  void set_power(bool on) override;
  void write(const uint8_t* data, size_t n) override;
  int read_byte() override;

  uint32_t rx_overflows() const;

 private:
  RadioPins pins_;
  uart_inst_t* uart_ = nullptr;
};

}  // namespace wxp
