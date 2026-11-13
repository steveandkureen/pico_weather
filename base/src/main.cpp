// Base station: receive LoRa packets, keep rain history, serve the iPad page.
//
// One core, one cooperative loop (docs/plan.md, "Firmware architecture").
// WiFi credentials come from, in order: a dev build override
// (WX_DEV_WIFI_SSID via CMake), then settings saved in flash, then setup mode.

#include <cstdio>

#include "app.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace {

constexpr uint32_t kWatchdogMs = 8000;

wxb::App g_app;

}  // namespace

// SNTP hook (see lwipopts.h). Runs inside cyw43_arch_poll(), i.e. the main loop.
extern "C" void wx_sntp_set_time(unsigned int sec) { g_app.on_time(sec); }

// Linker symbol: end of the firmware image in flash.
extern "C" char __flash_binary_end;
extern "C" uint32_t wxp_flash_region_start();

int main() {
  stdio_init_all();

  // The WiFi chip firmware upload takes a moment; enable the watchdog after.
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0) {
    std::printf("cyw43 init failed; rebooting in 5 s\n");
    sleep_ms(5000);
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
  }
  watchdog_enable(kWatchdogMs, true);

  const uintptr_t image_end = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
  if (image_end > wxp_flash_region_start()) {
    std::printf("FATAL: firmware overlaps the settings area in flash\n");
  }

  std::printf("base station %s starting\n", WX_VERSION);
  g_app.init();

  for (;;) {
    g_app.loop_once();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
  }
}
