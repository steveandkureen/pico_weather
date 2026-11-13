// Keeps the Pico 2 W on WiFi, without ever blocking the main loop.
//
// Escalation when the link is down:
//   1. Start a join; retry every kRetryMs.
//   2. Down for kChipResetMs: power-cycle the WiFi chip (deinit + init).
//   3. Down for kRebootMs: reboot the whole board through the watchdog.
// Rain history is in flash, so step 3 is harmless.
#pragma once

#include <cstdint>

namespace wx {

class WifiSupervisor {
 public:
  static constexpr uint32_t kRetryMs = 30 * 1000;
  static constexpr uint32_t kChipResetMs = 10 * 60 * 1000;
  static constexpr uint32_t kRebootMs = 30 * 60 * 1000;

  WifiSupervisor(const char* ssid, const char* password)
      : ssid_(ssid), password_(password) {}

  // Call often from the main loop. Returns true while the link has an IP.
  bool poll(uint32_t now_ms);

  bool up() const { return up_; }
  uint32_t reconnects() const { return reconnects_; }
  uint32_t chip_resets() const { return chip_resets_; }

 private:
  void start_join(uint32_t now_ms);

  const char* ssid_;
  const char* password_;
  bool up_ = false;
  bool joining_ = false;
  uint32_t join_started_ms_ = 0;
  uint32_t last_up_ms_ = 0;
  uint32_t last_chip_reset_ms_ = 0;
  uint32_t reconnects_ = 0;
  uint32_t chip_resets_ = 0;
};

}  // namespace wx
