#include "wxp/pico_radio_port.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/time.h"

namespace wxp {
namespace {

// Receive ring buffer filled by the UART interrupt. Single producer (IRQ),
// single consumer (main loop); indices are 32-bit so reads/writes are atomic.
constexpr uint32_t kRingSize = 512;  // power of two
uint8_t g_ring[kRingSize];
volatile uint32_t g_head = 0;  // written by IRQ
volatile uint32_t g_tail = 0;  // written by main loop
volatile uint32_t g_overflows = 0;
uart_inst_t* g_uart = nullptr;

void on_uart_rx() {
  while (uart_is_readable(g_uart)) {
    const uint8_t b = static_cast<uint8_t>(uart_get_hw(g_uart)->dr & 0xFF);
    const uint32_t head = g_head;
    if (head - g_tail < kRingSize) {
      g_ring[head & (kRingSize - 1)] = b;
      g_head = head + 1;
    } else {
      g_overflows = g_overflows + 1;
    }
  }
}

}  // namespace

void PicoRadioPort::init() {
  uart_ = pins_.uart_index == 0 ? uart0 : uart1;
  g_uart = uart_;

  // Power switch first, off: the radio module boots only when start() asks.
  gpio_init(pins_.power_en);
  gpio_put(pins_.power_en, false);
  gpio_set_dir(pins_.power_en, GPIO_OUT);

  // M0/M1 held low. With the module's default AT+SWITCH=0 it drives them low
  // itself, so this matches; if SWITCH were ever 1, low/low selects the normal
  // always-listening mode. (Never drive them high: that fights the module's
  // outputs, and with SWITCH=1 high/high is a sleep the UART can't wake.)
  gpio_init(pins_.m0);
  gpio_init(pins_.m1);
  gpio_put(pins_.m0, false);
  gpio_put(pins_.m1, false);
  gpio_set_dir(pins_.m0, GPIO_OUT);
  gpio_set_dir(pins_.m1, GPIO_OUT);

  gpio_init(pins_.aux);
  gpio_set_dir(pins_.aux, GPIO_IN);
  gpio_pull_up(pins_.aux);  // AUX high = busy, so an unplugged module reads "busy"

  uart_init(uart_, pins_.baud);
  uart_set_format(uart_, 8, 1, UART_PARITY_NONE);
  uart_set_hw_flow(uart_, false, false);
  uart_set_fifo_enabled(uart_, true);
  gpio_set_function(pins_.tx, GPIO_FUNC_UART);
  gpio_set_function(pins_.rx, GPIO_FUNC_UART);
  gpio_pull_up(pins_.rx);  // idle-high line while the module is off

  const int irq = uart_ == uart0 ? UART0_IRQ : UART1_IRQ;
  irq_set_exclusive_handler(irq, on_uart_rx);
  irq_set_enabled(irq, true);
  uart_set_irq_enables(uart_, true, false);
}

bool PicoRadioPort::aux_busy() { return gpio_get(pins_.aux); }

void PicoRadioPort::set_power(bool on) {
  if (!on) {
    // Don't back-power the module through the UART TX pin while it's off.
    gpio_set_function(pins_.tx, GPIO_FUNC_SIO);
    gpio_set_dir(pins_.tx, GPIO_IN);
  }
  gpio_put(pins_.power_en, on);
  if (on) gpio_set_function(pins_.tx, GPIO_FUNC_UART);
}

void PicoRadioPort::write(const uint8_t* data, size_t n) {
  // At most ~36 bytes; the 32-byte TX FIFO drains at ~1 ms/byte, so this
  // waits at most ~10 ms. Bounded, no loop on an external condition.
  for (size_t i = 0; i < n; ++i) {
    absolute_time_t until = make_timeout_time_ms(50);
    while (!uart_is_writable(uart_)) {
      if (time_reached(until)) return;
    }
    uart_get_hw(uart_)->dr = data[i];
  }
}

int PicoRadioPort::read_byte() {
  const uint32_t tail = g_tail;
  if (tail == g_head) return -1;
  const uint8_t b = g_ring[tail & (kRingSize - 1)];
  g_tail = tail + 1;
  return b;
}

uint32_t PicoRadioPort::rx_overflows() const { return g_overflows; }

}  // namespace wxp
