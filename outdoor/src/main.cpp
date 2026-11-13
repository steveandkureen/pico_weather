// Outdoor station: measure, send one LoRa packet every 30 s, repeat.
// Nothing is saved, so a reboot at any moment loses nothing.
//
// Structure: one core, one cooperative loop (see docs/plan.md, "Firmware
// architecture"). Every task's poll() returns quickly; the watchdog is fed
// only while every task keeps making progress.

#include <cstdio>
#include <initializer_list>

#include "board.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "wx/bme280.h"
#include "wx/e220.h"
#include "wx/health.h"
#include "wx/packet.h"
#include "wx/wind.h"
#include "wxp/pico_e220_port.h"
#include "wxp/pico_i2c_bus.h"

namespace {

constexpr uint32_t kWatchdogMs = 8000;
constexpr uint32_t kSendIntervalMs = 30000;
constexpr uint32_t kSensorIntervalMs = 10000;
constexpr uint32_t kRadioVerifyMs = 60 * 60 * 1000;
constexpr uint32_t kLedActiveMs = 10 * 60 * 1000;  // blink on send only for the first 10 min
constexpr uint8_t kStationId = 1;

constexpr uint64_t kRainDebounceUs = 50000;  // tipping-bucket switch bounce
constexpr uint64_t kWindDebounceUs = 2000;   // > 2 ms apart = up to 500 Hz

// ---------------------------------------------------------------- pulse counters (IRQ)

volatile uint32_t g_rain_tips = 0;
volatile uint64_t g_rain_last_us = 0;
volatile uint32_t g_wind_pulses = 0;
volatile uint64_t g_wind_last_us = 0;

void on_gpio(uint gpio, uint32_t events) {
  if (!(events & GPIO_IRQ_EDGE_FALL)) return;
  const uint64_t now = time_us_64();
  if (gpio == board::kRainGaugePin) {
    if (now - g_rain_last_us >= kRainDebounceUs) {
      g_rain_tips = g_rain_tips + 1;
      g_rain_last_us = now;
    }
  } else if (gpio == board::kAnemometerPin) {
    if (now - g_wind_last_us >= kWindDebounceUs) {
      g_wind_pulses = g_wind_pulses + 1;
      g_wind_last_us = now;
    }
  }
}

void pulses_init() {
  for (unsigned pin : {board::kRainGaugePin, board::kAnemometerPin}) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);  // backup; the board has external 10k pull-ups
  }
  gpio_set_irq_enabled_with_callback(board::kRainGaugePin, GPIO_IRQ_EDGE_FALL, true, &on_gpio);
  gpio_set_irq_enabled(board::kAnemometerPin, GPIO_IRQ_EDGE_FALL, true);
}

// ---------------------------------------------------------------- analog

uint16_t adc_avg(unsigned input, int samples) {
  adc_select_input(input);
  uint32_t sum = 0;
  for (int i = 0; i < samples; ++i) sum += adc_read();
  return static_cast<uint16_t>(sum / static_cast<uint32_t>(samples));
}

uint16_t adc_mv(uint16_t raw, float divider) {
  return static_cast<uint16_t>(static_cast<float>(raw) * 3300.0f / 4096.0f * divider + 0.5f);
}

// ---------------------------------------------------------------- hardware objects

wxp::PicoI2cBus g_i2c(i2c0, board::kI2cSda, board::kI2cScl, board::kI2cHz);
wx::Bme280 g_bme(g_i2c, board::kBme280Addr, kSensorIntervalMs);
wxp::PicoE220Port g_radio_port({board::kLoraUart, board::kLoraBaud, board::kLoraTx, board::kLoraRx,
                                board::kLoraM0, board::kLoraM1, board::kLoraAux, board::kLoraPowerEn});
wx::E220Radio g_radio(g_radio_port, wx::E220Config{});
wx::WindSampler g_wind;
wx::HealthMonitor g_health;

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

}  // namespace

int main() {
  stdio_init_all();

  const bool watchdog_reset = watchdog_enable_caused_reboot();
  const wx::ResetReason why = watchdog_reset ? wx::ResetReason::kWatchdog : wx::ResetReason::kPowerOn;
  watchdog_enable(kWatchdogMs, true);  // true = pause while a debugger is attached

  // Random per boot, so the base station can tell the tip counter restarted.
  const uint16_t boot_id = static_cast<uint16_t>(get_rand_32());
  std::printf("outdoor: boot id %04x (%s)\n", boot_id, watchdog_reset ? "watchdog reset" : "power on");

  gpio_init(board::kLedPin);
  gpio_set_dir(board::kLedPin, GPIO_OUT);

  pulses_init();
  adc_init();
  adc_gpio_init(board::kWindVanePin);
  adc_gpio_init(board::kBatteryPin);
  adc_gpio_init(board::kSolarPin);
  adc_gpio_init(board::kVsysPin);

  g_i2c.init();
  g_radio_port.init();
  uint32_t now = to_ms_since_boot(get_absolute_time());
  g_radio.start(now);

  const int t_radio = g_health.add("radio", 15000);
  const int t_bme = g_health.add("bme280", 15000);
  const int t_tick = g_health.add("tick", 5000);

  uint32_t next_second = now + 1000;
  uint32_t next_send = now + 5000;  // first packet soon after boot
  uint32_t next_verify = now + kRadioVerifyMs;
  uint32_t tick_progress = 0;
  uint32_t last_wind_count = 0;
  uint32_t bme_errors_at_last_send = 0;
  uint32_t led_off_at = 0;
  bool led_on = false;
  uint16_t seq = 0;
  uint16_t vane_rail_seconds = 0, vane_seconds = 0;

  for (;;) {
    const uint32_t loop_start = to_ms_since_boot(get_absolute_time());
    now = loop_start;

    g_radio.poll(now);
    g_bme.poll(now);

    // Once a second: wind pulses and vane.
    if (reached(now, next_second)) {
      next_second += 1000;
      if (reached(now, next_second)) next_second = now + 1000;  // fell behind: resync, don't burst
      const uint32_t count = g_wind_pulses;
      const uint32_t pulses = count - last_wind_count;
      last_wind_count = count;
      const uint16_t raw = adc_avg(board::kWindVanePin - 26, 8);
      ++vane_seconds;
      if (raw < 8 || raw > 4087) ++vane_rail_seconds;  // stuck at a rail = disconnected
      g_wind.add_second(static_cast<uint16_t>(pulses > 0xFFFF ? 0xFFFF : pulses), wx::as5600_adc_to_deg(raw));
      ++tick_progress;
    }

    // Every 30 s: build and send a packet.
    if (reached(now, next_send)) {
      next_send += kSendIntervalMs;
      if (reached(now, next_send)) next_send = now + kSendIntervalMs;

      wx::OutdoorReading r;
      r.station_id = kStationId;
      r.boot_id = boot_id;
      r.seq = seq++;
      r.uptime_s = static_cast<uint32_t>(time_us_64() / 1000000u);
      r.reset_reason = why;
      r.valid = 0;

      const wx::Bme280::Reading& b = g_bme.reading();
      if (b.valid && now - b.at_ms < 3 * kSensorIntervalMs) {
        r.temp_c_x100 = b.temp_c_x100;
        r.pressure_pa = b.pressure_pa;
        r.valid |= wx::kValidPressure;
        if (b.has_humidity) {
          r.humidity_x100 = b.humidity_x100;
          r.valid |= wx::kValidTempHumidity;
        }
      }

      const wx::WindSampler::Result w = g_wind.take();
      r.wind_pulses = w.pulses;
      r.wind_interval_ms = w.interval_ms;
      r.gust_pulses_3s = w.gust_pulses_3s;
      if (w.interval_ms > 0) r.valid |= wx::kValidWind;
      r.wind_dir_raw_deg = w.dir_deg;
      if (w.dir_deg != wx::kNoWindDir && vane_rail_seconds < vane_seconds) r.valid |= wx::kValidWindDir;
      vane_rail_seconds = vane_seconds = 0;

      r.rain_tips = g_rain_tips;

      const uint16_t batt = adc_mv(adc_avg(board::kBatteryPin - 26, 16), board::kBatteryDivider);
      const uint16_t solar = adc_mv(adc_avg(board::kSolarPin - 26, 16), board::kSolarDivider);
      if (batt > 500) {  // below that: divider not wired
        r.battery_mv = batt;
        r.valid |= wx::kValidBattery;
      }
      if (solar > 100) {
        r.solar_mv = solar;
        r.valid |= wx::kValidSolar;
      }

      const uint32_t errs = g_bme.errors() - bme_errors_at_last_send;
      bme_errors_at_last_send = g_bme.errors();
      r.sensor_errors = static_cast<uint8_t>(errs > 255 ? 255 : errs);
      const uint32_t ml = g_health.take_max_loop_ms();
      r.max_loop_ms = static_cast<uint8_t>(ml > 255 ? 255 : ml);

      uint8_t buf[wx::kPacketSize];
      if (wx::encode(r, buf, sizeof(buf)) == wx::kPacketSize) {
        const bool sent = g_radio.send(buf, sizeof(buf), now);
        std::printf("seq=%u tips=%lu wind=%u gust=%u dir=%u t=%d %s\n", r.seq,
                    static_cast<unsigned long>(r.rain_tips), r.wind_pulses, r.gust_pulses_3s,
                    r.wind_dir_raw_deg, r.temp_c_x100, sent ? "sent" : "radio not ready");
        if (sent && now < kLedActiveMs) {
          gpio_put(board::kLedPin, true);
          led_on = true;
          led_off_at = now + 50;
        }
      }
    }

    if (led_on && reached(now, led_off_at)) {
      gpio_put(board::kLedPin, false);
      led_on = false;
    }

    if (reached(now, next_verify)) {
      next_verify = now + kRadioVerifyMs;
      g_radio.verify(now);
    }

    // Watchdog gate.
    g_health.report(t_radio, g_radio.progress(), now);
    g_health.report(t_bme, g_bme.progress(), now);
    g_health.report(t_tick, tick_progress, now);
    const uint32_t end = to_ms_since_boot(get_absolute_time());
    g_health.note_loop(end - loop_start);
    if (g_health.all_ok(end)) {
      watchdog_update();
    } else {
      static bool logged = false;
      if (!logged) {
        std::printf("task stalled: %s; letting the watchdog reset\n", g_health.stalled(end));
        logged = true;
      }
    }

    // Sleep until an interrupt or 10 ms, whichever first.
    best_effort_wfe_or_timeout(make_timeout_time_ms(10));
  }
}
