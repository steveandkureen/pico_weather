#include "net/wifi_manager.h"

#include <cstdio>
#include <cstring>

#include "lwip/netif.h"
#include "net/setup_servers.h"
#include "pico/cyw43_arch.h"

namespace wxb {
namespace {

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

netif* sta_netif() { return &cyw43_state.netif[CYW43_ITF_STA]; }
netif* ap_netif() { return &cyw43_state.netif[CYW43_ITF_AP]; }

int scan_cb(void* env, const cyw43_ev_scan_result_t* r) {
  if (r != nullptr && r->ssid_len > 0) {
    static_cast<WifiManager*>(env)->add_scan_result(reinterpret_cast<const char*>(r->ssid), r->ssid_len,
                                                    r->rssi, r->auth_mode);
  }
  return 0;
}

}  // namespace

void WifiManager::begin(const char* hostname, uint32_t now_ms) {
  std::snprintf(hostname_, sizeof(hostname_), "%s", hostname);
  uint8_t mac[6] = {0};
  cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);
  std::snprintf(ap_ssid_, sizeof(ap_ssid_), "WeatherStation-%02X%02X", mac[4], mac[5]);
  // STA mode is always on: it's needed for scanning even in setup-only mode.
  cyw43_arch_enable_sta_mode();
  apply_hostname();
  down_since_ms_ = now_ms;
}

void WifiManager::apply_hostname() {
  // cyw43 creates the netif with a compile-time name; replace it so DHCP
  // (and the router's device list) shows ours. The string must stay valid.
  netif_set_hostname(sta_netif(), hostname_);
}

void WifiManager::set_hostname(const char* hostname) {
  std::snprintf(hostname_, sizeof(hostname_), "%s", hostname);
  apply_hostname();
}

void WifiManager::set_network(const char* ssid, const char* pass, uint32_t now_ms) {
  std::snprintf(ssid_, sizeof(ssid_), "%s", ssid);
  std::snprintf(pass_, sizeof(pass_), "%s", pass);
  have_network_ = ssid_[0] != '\0';
  // Drop the current link and join the new network on the next poll.
  if (sta_up_ || joining_) cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  sta_up_ = false;
  joining_ = false;
  down_since_ms_ = now_ms;
  join_started_ms_ = now_ms - kRetryMs;  // retry immediately
}

void WifiManager::start_join(uint32_t now_ms) {
  joining_ = true;
  join_started_ms_ = now_ms;
  ++reconnects_;
  const uint32_t auth = pass_[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN;
  cyw43_arch_wifi_connect_async(ssid_, pass_[0] ? pass_ : nullptr, auth);
}

void WifiManager::set_ap(bool on) {
  if (on == ap_on_) return;
  if (on) {
    std::printf("wifi: setup network '%s' on\n", ap_ssid_);
    cyw43_arch_enable_ap_mode(ap_ssid_, nullptr, CYW43_AUTH_OPEN);
    // Bringing the AP interface up makes it lwIP's default route; internet
    // traffic (NTP, forecast) must keep going out the home network.
    netif_set_default(sta_netif());
    setup_servers::start(ap_netif());
  } else {
    std::printf("wifi: setup network off\n");
    setup_servers::stop();
    cyw43_arch_disable_ap_mode();
    netif_set_default(sta_netif());
  }
  ap_on_ = on;
}

void WifiManager::open_setup(uint32_t now_ms) {
  setup_on_demand_ = true;
  setup_until_ms_ = now_ms + kSetupOnDemandMs;
  set_ap(true);
}

uint32_t WifiManager::ap_ip() const { return ip4_addr_get_u32(netif_ip4_addr(ap_netif())); }

void WifiManager::reset_chip(uint32_t now_ms) {
  std::printf("wifi: power-cycling the WiFi chip\n");
  ++chip_resets_;
  last_chip_reset_ms_ = now_ms;
  const bool ap_was_on = ap_on_;
  if (ap_on_) setup_servers::stop();
  ap_on_ = false;
  cyw43_arch_deinit();
  if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0) {
    // The chip won't come back. Stop feeding the watchdog by spinning here;
    // a full reboot is the last resort for a dead WiFi chip.
    for (;;) tight_loop_contents();
  }
  cyw43_arch_enable_sta_mode();
  apply_hostname();
  joining_ = false;
  sta_up_ = false;
  if (ap_was_on) set_ap(true);
}

void WifiManager::poll(uint32_t now_ms) {
  ++progress_;
  const int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  const bool up = have_network_ && status == CYW43_LINK_UP;

  if (up) {
    if (!sta_up_) {
      std::snprintf(ip_str_, sizeof(ip_str_), "%s", ip4addr_ntoa(netif_ip4_addr(sta_netif())));
      std::printf("wifi: connected to '%s' as %s\n", ssid_, ip_str_);
      up_since_ms_ = now_ms;
      link_up_event_ = true;
      netif_set_default(sta_netif());
    }
    sta_up_ = true;
    joining_ = false;
    down_since_ms_ = now_ms;
  } else {
    if (sta_up_) {
      std::printf("wifi: lost connection (status %d)\n", status);
      ip_str_[0] = '\0';
    }
    sta_up_ = false;
  }

  // Setup network policy.
  bool want_ap;
  if (!have_network_) {
    want_ap = true;
  } else if (setup_on_demand_ && !reached(now_ms, setup_until_ms_)) {
    want_ap = true;
  } else if (!up && now_ms - down_since_ms_ >= kSetupAfterMs) {
    want_ap = true;
  } else if (up && ap_on_ && !setup_on_demand_) {
    want_ap = now_ms - up_since_ms_ < kSetupLingerMs;
  } else {
    want_ap = ap_on_ && !up;
  }
  if (setup_on_demand_ && reached(now_ms, setup_until_ms_)) setup_on_demand_ = false;
  if (want_ap != ap_on_) set_ap(want_ap);

  if (!have_network_ || up) return;

  // Joining and recovery.
  const uint32_t down_ms = now_ms - down_since_ms_;
  if (down_ms >= kChipResetMs && now_ms - last_chip_reset_ms_ >= kChipResetMs * 2) {
    reset_chip(now_ms);
    return;
  }
  const bool failed = status == CYW43_LINK_FAIL || status == CYW43_LINK_NONET ||
                      status == CYW43_LINK_BADAUTH || status == CYW43_LINK_DOWN;
  if (!joining_ || (failed && now_ms - join_started_ms_ >= kRetryMs) ||
      now_ms - join_started_ms_ >= 2 * kRetryMs) {
    start_join(now_ms);
  }
}

bool WifiManager::start_scan() {
  if (scanning()) return true;
  scan_n_ = 0;
  cyw43_wifi_scan_options_t opts = {};
  return cyw43_wifi_scan(&cyw43_state, &opts, this, scan_cb) == 0;
}

bool WifiManager::scanning() const { return cyw43_wifi_scan_active(&cyw43_state); }

int WifiManager::scan_results(const ScanResult** out) const {
  *out = scan_;
  return scan_n_;
}

void WifiManager::add_scan_result(const char* ssid, int len, int16_t rssi, uint8_t auth) {
  if (len <= 0 || len > 32) return;
  char name[33];
  std::memcpy(name, ssid, static_cast<size_t>(len));
  name[len] = '\0';
  for (int i = 0; i < scan_n_; ++i) {
    if (std::strcmp(scan_[i].ssid, name) == 0) {
      if (rssi > scan_[i].rssi) scan_[i].rssi = rssi;
      return;
    }
  }
  if (scan_n_ >= kMaxScan) {
    // Replace the weakest if this one is stronger.
    int weakest = 0;
    for (int i = 1; i < scan_n_; ++i) {
      if (scan_[i].rssi < scan_[weakest].rssi) weakest = i;
    }
    if (rssi <= scan_[weakest].rssi) return;
    std::memcpy(scan_[weakest].ssid, name, sizeof(name));
    scan_[weakest].rssi = rssi;
    scan_[weakest].auth = auth;
    return;
  }
  std::memcpy(scan_[scan_n_].ssid, name, sizeof(name));
  scan_[scan_n_].rssi = rssi;
  scan_[scan_n_].auth = auth;
  ++scan_n_;
}

}  // namespace wxb
