// Base station application: owns every task and serves the web pages.
#pragma once

#include <cstdint>

#include "net/forecast_client.h"
#include "net/http_server.h"
#include "net/wifi_manager.h"
#include "wx/bme280.h"
#include "wx/dxlr22.h"
#include "wx/flash_store.h"
#include "wx/health.h"
#include "wx/settings.h"
#include "wx/station.h"
#include "wxp/pico_radio_port.h"
#include "wxp/pico_flash.h"
#include "wxp/pico_i2c_bus.h"

namespace wxb {

class App : public HttpHandler {
 public:
  App();
  void init();
  void loop_once();

  // HttpHandler
  void handle(const wx::HttpRequest& req, uint32_t local_ip, HttpResponse& resp) override;

  // Called from the SNTP hook.
  void on_time(uint32_t unix_s);

 private:
  void load_storage();
  void save_settings();
  void maybe_save_history(uint32_t now, bool after_packet);
  void poll_radio(uint32_t now);
  void poll_indoor(uint32_t now);
  void poll_button(uint32_t now);
  void poll_network(uint32_t now);
  void update_led(uint32_t now);

  void serve_asset(const char* name, HttpResponse& r);
  void serve_json_error(HttpResponse& r, int status, const char* msg);
  wx::Station::SysInfo sys_info();

  // Storage
  wxp::PicoFlash flash_;
  wx::RecordStore settings_store_;
  wx::RecordStore history_store_;
  wx::Settings settings_;
  bool storage_ok_ = true;
  uint32_t last_history_save_ms_ = 0;
  int32_t last_saved_day_ = 0;

  // Model
  wx::Station station_;

  // Hardware
  wxp::PicoI2cBus i2c_;
  wx::Bme280 indoor_;
  uint32_t last_indoor_at_ = 0;
  uint32_t next_indoor_report_ = 0;
  wxp::PicoRadioPort radio_port_;
  wx::Dxlr22Radio radio_;
  uint32_t last_packet_ms_ = 0;
  int radio_recovery_level_ = 0;
  uint32_t next_radio_recovery_ms_ = 0;
  uint32_t next_radio_verify_ = 0;

  // Network
  WifiManager wifi_;
  ForecastClient forecast_;
  bool net_services_started_ = false;
  bool mdns_added_ = false;
  int8_t mdns_slot_ = -1;
  bool sntp_started_ = false;

  // Button / LED
  bool button_down_ = false;
  uint32_t button_since_ = 0;
  bool led_ = false;

  // Pending actions requested over HTTP (run after the response goes out).
  uint32_t reboot_at_ = 0;
  bool reboot_pending_ = false;
  bool wipe_pending_ = false;
  uint32_t last_pin_fail_ms_ = 0;

  // Watchdog gate
  wx::HealthMonitor health_;
  int t_radio_ = -1, t_indoor_ = -1, t_net_ = -1;
  bool watchdog_boot_ = false;
};

}  // namespace wxb
