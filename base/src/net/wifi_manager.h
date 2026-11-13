// Keeps the Pico 2 W on WiFi and runs the setup network when needed.
//
// * No saved network → setup network only ("WeatherStation-XXXX", open).
// * Saved network → join it (async, never blocks), retry every 30 s.
// * Down for 10 min → setup network comes up alongside, so settings can be
//   fixed from a phone; it goes away again once the home network has been
//   back for a minute.
// * Down for 30 min → power-cycle the WiFi chip (repeats hourly). No
//   reboots: rain history is safe either way, but a reboot loop would also
//   take the setup network down.
// * The setup button can open the setup network on demand for 30 min.
#pragma once

#include <cstdint>

namespace wxb {

struct ScanResult {
  char ssid[33];
  int16_t rssi;
  uint8_t auth;  // 0 = open
};

class WifiManager {
 public:
  static constexpr uint32_t kRetryMs = 30 * 1000;
  static constexpr uint32_t kSetupAfterMs = 10 * 60 * 1000;
  static constexpr uint32_t kChipResetMs = 30 * 60 * 1000;
  static constexpr uint32_t kSetupLingerMs = 60 * 1000;
  static constexpr uint32_t kSetupOnDemandMs = 30 * 60 * 1000;
  static constexpr int kMaxScan = 16;

  // Call once after cyw43_arch_init().
  void begin(const char* hostname, uint32_t now_ms);

  // Change the network to join (empty ssid = none). Takes effect immediately.
  void set_network(const char* ssid, const char* pass, uint32_t now_ms);
  void set_hostname(const char* hostname);

  void poll(uint32_t now_ms);

  // Opens the setup network for kSetupOnDemandMs.
  void open_setup(uint32_t now_ms);

  bool sta_up() const { return sta_up_; }
  bool setup_active() const { return ap_on_; }
  const char* setup_ssid() const { return ap_ssid_; }
  const char* ip_str() const { return ip_str_; }
  uint32_t ap_ip() const;  // network order
  uint32_t reconnects() const { return reconnects_; }
  uint32_t chip_resets() const { return chip_resets_; }
  uint32_t progress() const { return progress_; }
  // True once each time the link (re)gains an IP: re-announce mDNS etc.
  bool take_link_up_event() {
    bool e = link_up_event_;
    link_up_event_ = false;
    return e;
  }

  // Scanning (for the setup page's network list).
  bool start_scan();
  bool scanning() const;
  int scan_results(const ScanResult** out) const;
  void add_scan_result(const char* ssid, int len, int16_t rssi, uint8_t auth);

 private:
  void start_join(uint32_t now_ms);
  void set_ap(bool on);
  void reset_chip(uint32_t now_ms);
  void apply_hostname();

  char ssid_[33] = {};
  char pass_[65] = {};
  char hostname_[32] = "weather";
  char ap_ssid_[24] = "WeatherStation";
  char ip_str_[16] = "";
  bool have_network_ = false;
  bool sta_up_ = false;
  bool joining_ = false;
  bool ap_on_ = false;
  bool link_up_event_ = false;
  uint32_t join_started_ms_ = 0;
  uint32_t down_since_ms_ = 0;
  uint32_t up_since_ms_ = 0;
  uint32_t last_chip_reset_ms_ = 0;
  uint32_t setup_until_ms_ = 0;
  bool setup_on_demand_ = false;
  uint32_t reconnects_ = 0;
  uint32_t chip_resets_ = 0;
  uint32_t progress_ = 0;
  ScanResult scan_[kMaxScan];
  int scan_n_ = 0;
};

}  // namespace wxb
