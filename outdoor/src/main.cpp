// Outdoor station: measure, send one LoRa packet, repeat. Nothing is saved, so
// a reboot at any moment loses nothing.
//
// Stability rules:
//   * Hardware watchdog: if the main loop stops for kWatchdogMs, the chip resets.
//   * No heap allocation after startup.
//   * Every sensor call has a time limit; a failed sensor clears its valid bit.
//
// TODO once the parts list arrives: real sensor drivers, LoRa radio, pin numbers.

#include <cstdio>

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "board.h"
#include "wx/packet.h"

namespace {

constexpr uint32_t kWatchdogMs = 5000;
constexpr uint32_t kSendIntervalMs = 30000;
constexpr uint8_t kStationId = 1;

constexpr uint kRainGaugePin = board::kRainGaugePin;
constexpr uint32_t kRainDebounceUs = 50000;  // reed switch bounce

// Updated only from the GPIO interrupt.
volatile uint32_t g_rain_tips = 0;
volatile uint64_t g_last_tip_us = 0;

void gpio_irq(uint gpio, uint32_t events) {
  if (gpio == kRainGaugePin && (events & GPIO_IRQ_EDGE_FALL)) {
    const uint64_t now = time_us_64();
    if (now - g_last_tip_us >= kRainDebounceUs) {
      g_rain_tips = g_rain_tips + 1;
      g_last_tip_us = now;
    }
  }
}

void rain_init() {
  gpio_init(kRainGaugePin);
  gpio_set_dir(kRainGaugePin, GPIO_IN);
  gpio_pull_up(kRainGaugePin);  // backup only; the board has an external 10k pull-up
  gpio_set_irq_enabled_with_callback(kRainGaugePin, GPIO_IRQ_EDGE_FALL, true, &gpio_irq);
}

uint32_t rain_tips() {
  // A 32-bit aligned read is atomic on Cortex-M33.
  return g_rain_tips;
}

}  // namespace

int main() {
  stdio_init_all();

  const wx::ResetReason why =
      watchdog_caused_reboot() ? wx::ResetReason::kWatchdog : wx::ResetReason::kPowerOn;
  watchdog_enable(kWatchdogMs, true);  // true = pause while a debugger is attached

  // Random per boot, so the base station can tell the tip counter restarted.
  const uint16_t boot_id = static_cast<uint16_t>(get_rand_32());

  rain_init();
  // TODO: sensors_init(), wind_init(), radio_init()

  uint16_t seq = 0;
  absolute_time_t next_send = get_absolute_time();
  uint8_t buf[wx::kPacketSize];

  for (;;) {
    watchdog_update();

    if (time_reached(next_send)) {
      next_send = delayed_by_ms(next_send, kSendIntervalMs);

      wx::OutdoorReading r;
      r.station_id = kStationId;
      r.boot_id = boot_id;
      r.seq = seq++;
      r.uptime_s = static_cast<uint32_t>(time_us_64() / 1000000);
      r.reset_reason = why;
      r.valid = 0;  // TODO: set bits as each real sensor reports in
      r.rain_tips = rain_tips();

      if (wx::encode(r, buf, sizeof(buf)) == wx::kPacketSize) {
        // TODO: radio_send(buf, sizeof(buf)) with its own timeout.
        std::printf("seq=%u tips=%lu\n", r.seq, static_cast<unsigned long>(r.rain_tips));
      }
    }

    sleep_ms(50);
  }
}
