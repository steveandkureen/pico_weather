// Base station: receive LoRa packets, keep rain history, serve the iPad page.
//
// One core, one cooperative loop (docs/plan.md, "Firmware architecture").
// WiFi credentials come from, in order: a dev build override
// (WX_DEV_WIFI_SSID via CMake), then settings saved in flash, then setup mode.

#include <cstdio>

#include "app.h"
#include "crashlog.h"
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
  crashlog::boot();
  crashlog::crumb(1);
  stdio_init_all();

  // Why the previous run ended (reset cause, last step, fault PC) is kept for
  // GET /api/http; print it too in case a USB console is attached.
  {
    char line[200];
    crashlog::to_json(line, sizeof(line));
    std::printf("boot: %s\n", line);
  }

  // The WiFi chip firmware upload takes a moment; enable the watchdog after.
  crashlog::crumb(2);
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0) {
    std::printf("cyw43 init failed; rebooting in 5 s\n");
    sleep_ms(5000);
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
  }
  crashlog::crumb(3);
  watchdog_enable(kWatchdogMs, true);

  const uintptr_t image_end = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
  if (image_end > wxp_flash_region_start()) {
    std::printf("FATAL: firmware overlaps the settings area in flash\n");
  }

  std::printf("base station %s starting\n", WX_VERSION);
  crashlog::crumb(4);
  g_app.init();

  bool settled = false;
  for (;;) {
    g_app.loop_once();
    crashlog::crumb(120);
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
    if (!settled && to_ms_since_boot(get_absolute_time()) > 20000) {
      crashlog::settled();
      settled = true;
    }
  }
}
