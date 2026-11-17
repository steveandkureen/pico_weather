// Bench tool, not station firmware: connects the USB serial port straight to
// the LoRa module's UART so a PC can talk to it (AT commands, raw data).
//
//   Build target wx_radio_bridge; flash it, open /dev/ttyACM0, type away.
//   Lines starting with '!' are for the bridge itself, not the module:
//     !m0=0 / !m0=1 / !m1=0 / !m1=1   set the mode pins
//     !baud=115200                    change the module UART baud rate
//     !aux                            print the AUX level now
//     !probe                          is anything driving AUX and module TXD?
//     !edges                          send "+++\r\n", count edges on module TXD for 1.5 s
//   M0/M1 start as inputs: by default (AT+SWITCH=0) the DX-LR22 drives them.
//   The bridge prints "[aux=H]" / "[aux=L]" whenever AUX changes.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

namespace {

uart_inst_t* const kUart = board::kLoraUart == 0 ? uart0 : uart1;

// Reads a pin with the pull-up, then the pull-down. A pin the module drives
// reads the same both ways; a disconnected one follows the pull.
const char* probe_pin(unsigned pin) {
  gpio_pull_up(pin);
  sleep_ms(5);
  const bool up = gpio_get(pin);
  gpio_pull_down(pin);
  sleep_ms(5);
  const bool down = gpio_get(pin);
  gpio_disable_pulls(pin);
  if (up && !down) return "floating (not connected?)";
  return up ? "driven high" : "driven low";
}

void handle_bridge_cmd(const char* line) {
  if (std::strcmp(line, "!probe") == 0) {
    gpio_set_function(board::kLoraRx, GPIO_FUNC_SIO);
    gpio_set_dir(board::kLoraRx, GPIO_IN);
    std::printf("[probe: AUX(GP%u) %s, module TXD(GP%u) %s]\n", board::kLoraAux, probe_pin(board::kLoraAux),
                board::kLoraRx, probe_pin(board::kLoraRx));
    gpio_pull_up(board::kLoraRx);
    gpio_set_function(board::kLoraRx, GPIO_FUNC_UART);
    return;
  }
  if (std::strcmp(line, "!edges") == 0) {
    while (uart_is_readable(kUart)) uart_getc(kUart);
    uart_puts(kUart, "+++\r\n");
    uart_tx_wait_blocking(kUart);
    int edges = 0, aux_edges = 0;
    bool last = gpio_get(board::kLoraRx), last_aux = gpio_get(board::kLoraAux);
    const absolute_time_t until = make_timeout_time_ms(1500);
    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
      const bool v = gpio_get(board::kLoraRx), a = gpio_get(board::kLoraAux);
      if (v != last) { ++edges; last = v; }
      if (a != last_aux) { ++aux_edges; last_aux = a; }
    }
    std::printf("[edges: module TXD %d, AUX %d; uart bytes waiting: %s]\n", edges, aux_edges,
                uart_is_readable(kUart) ? "yes" : "no");
    return;
  }
  if (std::strncmp(line, "!m0=", 4) == 0) {
    gpio_set_dir(board::kLoraM0, GPIO_OUT);
    gpio_put(board::kLoraM0, line[4] == '1');
  } else if (std::strncmp(line, "!m1=", 4) == 0) {
    gpio_set_dir(board::kLoraM1, GPIO_OUT);
    gpio_put(board::kLoraM1, line[4] == '1');
  } else if (std::strncmp(line, "!baud=", 6) == 0) {
    uart_set_baudrate(kUart, static_cast<uint>(std::atoi(line + 6)));
  }
  std::printf("[bridge: m0=%d m1=%d aux=%c]\n", gpio_get(board::kLoraM0),
              gpio_get(board::kLoraM1), gpio_get(board::kLoraAux) ? 'H' : 'L');
}

}  // namespace

int main() {
  stdio_init_all();

  uart_init(kUart, board::kLoraBaud);
  gpio_set_function(board::kLoraTx, GPIO_FUNC_UART);
  gpio_set_function(board::kLoraRx, GPIO_FUNC_UART);

  gpio_init(board::kLoraM0);
  gpio_set_dir(board::kLoraM0, GPIO_IN);  // module drives these by default
  gpio_init(board::kLoraM1);
  gpio_set_dir(board::kLoraM1, GPIO_IN);
  gpio_init(board::kLoraAux);
  gpio_set_dir(board::kLoraAux, GPIO_IN);  // no pull: see what the module drives
  gpio_init(board::kLoraPowerEn);
  gpio_set_dir(board::kLoraPowerEn, GPIO_OUT);
  gpio_put(board::kLoraPowerEn, true);

  gpio_init(board::kHeartbeatLedPin);
  gpio_set_dir(board::kHeartbeatLedPin, GPIO_OUT);

  bool aux = gpio_get(board::kLoraAux);
  char line[64];
  size_t len = 0;
  bool at_line_start = true;
  bool bridge_line = false;
  uint32_t last_blink = 0;

  for (;;) {
    // PC -> module (bridge commands are swallowed).
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
      if (at_line_start) bridge_line = (c == '!');
      at_line_start = (c == '\n');
      if (bridge_line) {
        if (c == '\n' || c == '\r') {
          line[len] = '\0';
          if (len > 0) handle_bridge_cmd(line);
          len = 0;
          bridge_line = false;
          at_line_start = true;
        } else if (len + 1 < sizeof(line)) {
          line[len++] = static_cast<char>(c);
        }
        continue;
      }
      uart_putc_raw(kUart, static_cast<char>(c));
    }
    // Module -> PC.
    while (uart_is_readable(kUart)) putchar_raw(uart_getc(kUart));

    const bool a = gpio_get(board::kLoraAux);
    if (a != aux) {
      aux = a;
      std::printf("[aux=%c]", aux ? 'H' : 'L');
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_blink >= 250) {  // fast blink = bridge, not station firmware
      last_blink = now;
      gpio_xor_mask(1u << board::kHeartbeatLedPin);
    }
  }
}
