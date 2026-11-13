// Base station: receive LoRa packets, keep rain history, serve the iPad page.
//
// Stability rules:
//   * Hardware watchdog on the main loop.
//   * No blocking calls: WiFi joins are async, everything is polled.
//   * No heap allocation after startup (lwIP uses its own fixed pools).
//
// WiFi credentials come from, in order: a dev build override
// (WX_DEV_WIFI_SSID, set via CMake), then settings saved in flash, then setup
// mode (the Pico hosts its own network with a config page). See docs/plan.md.
//
// TODO: settings in flash + setup mode, LoRa receive, indoor sensor, NTP time,
//       flash save of the rain ledger, web server.

#include <cstdio>

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "wifi_supervisor.h"
#include "wx/packet.h"
#include "wx/rain_ledger.h"

#ifndef WX_DEV_WIFI_SSID
#define WX_DEV_WIFI_SSID ""
#endif
#ifndef WX_DEV_WIFI_PASSWORD
#define WX_DEV_WIFI_PASSWORD ""
#endif

namespace {

constexpr uint32_t kWatchdogMs = 8000;
constexpr bool kDevWifi = sizeof(WX_DEV_WIFI_SSID) > 1;

wx::RainLedger g_ledger;  // static storage, not heap
// TODO: once settings live in flash, pass the saved network when !kDevWifi.
wx::WifiSupervisor g_wifi(WX_DEV_WIFI_SSID, WX_DEV_WIFI_PASSWORD);

}  // namespace

int main() {
  stdio_init_all();

  const bool watchdog_reboot_seen = watchdog_caused_reboot();
  watchdog_enable(kWatchdogMs, true);

  if (cyw43_arch_init() != 0) {
    std::printf("cyw43 init failed, waiting for watchdog reset\n");
    for (;;) tight_loop_contents();
  }
  cyw43_arch_enable_sta_mode();
  std::printf("base station start (watchdog reboot: %s)\n", watchdog_reboot_seen ? "yes" : "no");

  for (;;) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    cyw43_arch_poll();  // drive the WiFi chip and lwIP
    if (kDevWifi) {
      g_wifi.poll(now);
    }
    // TODO: else use saved settings, or run setup mode if there are none.
    // TODO: radio_poll() -> wx::decode() -> g_ledger.on_reading(...)

    watchdog_update();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(50));
  }
}
