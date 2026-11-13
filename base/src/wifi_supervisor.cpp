#include "wifi_supervisor.h"

#include <cstdio>

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"

namespace wx {

void WifiSupervisor::start_join(uint32_t now_ms) {
  joining_ = true;
  join_started_ms_ = now_ms;
  ++reconnects_;
  cyw43_arch_wifi_connect_async(ssid_, password_, CYW43_AUTH_WPA2_AES_PSK);
}

bool WifiSupervisor::poll(uint32_t now_ms) {
  const int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

  if (status == CYW43_LINK_UP) {
    if (!up_) std::printf("wifi: up\n");
    up_ = true;
    joining_ = false;
    last_up_ms_ = now_ms;
    return true;
  }

  if (up_) std::printf("wifi: down (status %d)\n", status);
  up_ = false;

  const uint32_t down_ms = now_ms - last_up_ms_;

  if (down_ms >= kRebootMs) {
    std::printf("wifi: down too long, rebooting\n");
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
  }

  if (down_ms >= kChipResetMs && now_ms - last_chip_reset_ms_ >= kChipResetMs) {
    std::printf("wifi: resetting chip\n");
    last_chip_reset_ms_ = now_ms;
    ++chip_resets_;
    cyw43_arch_deinit();
    if (cyw43_arch_init() != 0) {
      // Can't bring the chip back: let the watchdog reboot us.
      watchdog_reboot(0, 0, 0);
      for (;;) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();
    joining_ = false;
  }

  const bool join_failed = status == CYW43_LINK_FAIL || status == CYW43_LINK_NONET ||
                           status == CYW43_LINK_BADAUTH || status == CYW43_LINK_DOWN;
  if (!joining_ || (join_failed && now_ms - join_started_ms_ >= kRetryMs) ||
      now_ms - join_started_ms_ >= 2 * kRetryMs) {
    start_join(now_ms);
  }
  return false;
}

}  // namespace wx
