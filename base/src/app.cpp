#include "app.h"

#include "crashlog.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "web_assets.h"
#include "wx/config_form.h"
#include "wx/json.h"

#ifndef WX_DEV_WIFI_SSID
#define WX_DEV_WIFI_SSID ""
#endif
#ifndef WX_DEV_WIFI_PASSWORD
#define WX_DEV_WIFI_PASSWORD ""
#endif
#ifndef WX_VERSION
#define WX_VERSION "dev"
#endif

namespace wxb {
namespace {

constexpr bool kDevWifi = sizeof(WX_DEV_WIFI_SSID) > 1;
constexpr uint32_t kSettingsKind = 0x53455447;  // "SETG"
constexpr uint32_t kHistoryKind = 0x48495354;   // "HIST"
constexpr uint32_t kIndoorIntervalMs = 30000;
constexpr uint32_t kHistorySaveMs = 10 * 60 * 1000;
constexpr uint32_t kRadioSilenceMs = 10 * 60 * 1000;
constexpr uint32_t kRadioVerifyMs = 60 * 60 * 1000;
constexpr uint32_t kButtonLongMs = 10000;
constexpr uint32_t kPinFailDelayMs = 1000;

uint8_t g_scratch[wx::FlashDevice::kSectorSize];

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }
uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

netif* sta_netif() { return &cyw43_state.netif[CYW43_ITF_STA]; }

void mdns_txt(struct mdns_service* service, void*) {
  mdns_resp_add_service_txtitem(service, "path=/", 6);
}

bool body_has_pin(const wx::Settings& s, const wx::HttpRequest& req) {
  return wx::pin_ok(s, req.body, req.body_len);
}

}  // namespace

App::App()
    : settings_store_(flash_, wxp::PicoFlash::kSettingsA, wxp::PicoFlash::kSettingsB, kSettingsKind, g_scratch),
      history_store_(flash_, wxp::PicoFlash::kHistoryA, wxp::PicoFlash::kHistoryB, kHistoryKind, g_scratch),
      i2c_(i2c0, board::kI2cSda, board::kI2cScl, board::kI2cHz),
      indoor_(i2c_, board::kBme280Addr, kIndoorIntervalMs),
      radio_port_({board::kLoraUart, board::kLoraBaud, board::kLoraTx, board::kLoraRx, board::kLoraM0,
                   board::kLoraM1, board::kLoraAux, board::kLoraPowerEn}),
      radio_(radio_port_, wx::Dxlr22Config{}) {}

// ---------------------------------------------------------------- storage

void App::load_storage() {
  wx::settings_defaults(&settings_);
  const int n = settings_store_.load(&settings_, sizeof(settings_));
  if (n < 0) {
    std::printf("settings: none saved, using defaults\n");
    wx::settings_defaults(&settings_);
  } else {
    std::printf("settings: loaded (%d bytes, seq %lu)\n", n, static_cast<unsigned long>(settings_store_.seq()));
  }
  wx::settings_sanitize(&settings_);

  static wx::History h;  // ~850 bytes: keep it off the stack
  if (history_store_.load(&h, sizeof(h)) == static_cast<int>(sizeof(h))) {
    station_.load_history(h);
    std::printf("history: loaded (seq %lu)\n", static_cast<unsigned long>(history_store_.seq()));
  } else {
    std::printf("history: none saved\n");
  }
}

void App::save_settings() {
  if (!settings_store_.save(&settings_, sizeof(settings_))) {
    std::printf("settings: SAVE FAILED\n");
    station_.settings_save_failed();
  }
}

void App::maybe_save_history(uint32_t now, bool after_packet) {
  if (!station_.history_dirty()) return;
  const int32_t day = station_.today(time_us_64());
  const bool day_changed = day != last_saved_day_;
  const bool due = now - last_history_save_ms_ >= kHistorySaveMs;
  // Save right after a packet (the radio is quiet for ~30 s), or if packets
  // stopped coming, whenever it's due.
  const bool quiet = after_packet || now - last_packet_ms_ > 40000;
  if (!(quiet && (due || day_changed))) return;
  const bool ok = history_store_.save(&station_.history(), sizeof(wx::History));
  station_.history_saved(ok);
  last_history_save_ms_ = now;
  last_saved_day_ = day;
  if (!ok) std::printf("history: SAVE FAILED\n");
}

// ---------------------------------------------------------------- settings

void App::on_time(uint32_t unix_s) {
  station_.clock().sync(unix_s, time_us_64());
  std::printf("time: set from NTP (%lu)\n", static_cast<unsigned long>(unix_s));
}

// ---------------------------------------------------------------- init

void App::init() {
  watchdog_boot_ = watchdog_enable_caused_reboot();
  station_.set_base_restarted_by_watchdog(watchdog_boot_);

  crashlog::crumb(10);
  load_storage();

  crashlog::crumb(11);
  gpio_init(board::kSetupButtonPin);
  gpio_set_dir(board::kSetupButtonPin, GPIO_IN);
  gpio_pull_up(board::kSetupButtonPin);

  crashlog::crumb(12);
  i2c_.init();
  crashlog::crumb(13);
  radio_port_.init();
  crashlog::crumb(14);
  radio_.start(now_ms());
  last_packet_ms_ = now_ms();
  next_radio_recovery_ms_ = last_packet_ms_ + kRadioSilenceMs;

  crashlog::crumb(15);
  wifi_.begin(settings_.hostname, now_ms());
  crashlog::crumb(16);
  station_.set_settings(settings_);
  if (kDevWifi) {
    std::printf("wifi: DEV BUILD, using '%s'\n", WX_DEV_WIFI_SSID);
    wifi_.set_network(WX_DEV_WIFI_SSID, WX_DEV_WIFI_PASSWORD, now_ms());
  } else {
    wifi_.set_network(settings_.wifi_ssid, settings_.wifi_pass, now_ms());
  }

  crashlog::crumb(18);
  if (!http::start(this, 80)) std::printf("http: could not start server\n");
  crashlog::crumb(19);
  mdns_resp_init();

  crashlog::crumb(20);
  t_radio_ = health_.add("radio", 15000);
  t_indoor_ = health_.add("indoor", 15000);
  t_net_ = health_.add("network", 15000);
  next_radio_verify_ = now_ms() + kRadioVerifyMs;
  last_saved_day_ = station_.today(time_us_64());
}

// ---------------------------------------------------------------- tasks

void App::poll_radio(uint32_t now) {
  radio_.poll(now);
  wx::OutdoorReading r;
  int16_t rssi = 0;
  bool got = false;
  while (radio_.receive(now, &r, &rssi)) {
    station_.on_packet(r, rssi, now, time_us_64());
    last_packet_ms_ = now;
    radio_recovery_level_ = 0;
    next_radio_recovery_ms_ = now + kRadioSilenceMs;
    got = true;
  }
  station_.set_radio_ok(!radio_.dead(), now);

  // Nothing heard for a long time: maybe our radio is the problem. After
  // 10 min re-apply its settings; after that power-cycle it every 20 min.
  if (reached(now, next_radio_recovery_ms_)) {
    if (radio_recovery_level_ < 2) ++radio_recovery_level_;
    std::printf("radio: silent for %lu min, recovery level %d\n",
                static_cast<unsigned long>((now - last_packet_ms_) / 60000), radio_recovery_level_);
    radio_.recover(radio_recovery_level_, now);
    next_radio_recovery_ms_ = now + 2 * kRadioSilenceMs;
  }
  if (reached(now, next_radio_verify_)) {
    next_radio_verify_ = now + kRadioVerifyMs;
    radio_.verify(now);
  }
  maybe_save_history(now, got);
}

void App::poll_indoor(uint32_t now) {
  indoor_.poll(now);
  const wx::Bme280::Reading& r = indoor_.reading();
  if (r.valid && r.at_ms != last_indoor_at_) {
    last_indoor_at_ = r.at_ms;
    station_.on_indoor(true, r.temp_c_x100, r.humidity_x100, r.pressure_pa, r.has_humidity);
    next_indoor_report_ = now + 2 * kIndoorIntervalMs;
  } else if (reached(now, next_indoor_report_)) {
    station_.on_indoor(false, 0, 0, 0, false);  // no fresh reading in time
    next_indoor_report_ = now + kIndoorIntervalMs;
  }
}

void App::poll_button(uint32_t now) {
  const bool down = !gpio_get(board::kSetupButtonPin);
  if (down && !button_down_) {
    button_down_ = true;
    button_since_ = now;
  } else if (!down && button_down_) {
    button_down_ = false;
    const uint32_t held = now - button_since_;
    if (held >= 50 && held < 3000) {
      std::printf("button: opening setup network\n");
      wifi_.open_setup(now);
    }
  }
  if (button_down_ && now - button_since_ >= kButtonLongMs && !wipe_pending_) {
    std::printf("button: factory reset\n");
    wipe_pending_ = true;
    reboot_pending_ = true;
    reboot_at_ = now + 500;
  }
}

void App::poll_network(uint32_t now) {
  crashlog::crumb(203);
  cyw43_arch_poll();
  crashlog::crumb(204);
  wifi_.poll(now);
  crashlog::crumb(205);
  http::poll(now);

  crashlog::crumb(206);
  if (wifi_.take_link_up_event()) {
    if (!mdns_added_) {
      mdns_resp_add_netif(sta_netif(), settings_.hostname);
      mdns_slot_ = mdns_resp_add_service(sta_netif(), settings_.hostname, "_http", DNSSD_PROTO_TCP, 80,
                                         mdns_txt, nullptr);
      mdns_added_ = true;
    } else {
      mdns_resp_announce(sta_netif());
    }
    if (!sntp_started_) {
      sntp_setoperatingmode(SNTP_OPMODE_POLL);
      sntp_setservername(0, "pool.ntp.org");
      sntp_init();
      sntp_started_ = true;
    }
    forecast_.request_now();
  }

  crashlog::crumb(207);
  wx::Forecast f;
  if (forecast_.poll(now, wifi_.sta_up(), settings_.have_location, settings_.lat_e5, settings_.lon_e5, &f)) {
    station_.on_forecast(f, now);
    std::printf("forecast: updated (%d days)\n", f.ndays);
  }
}

void App::update_led(uint32_t now) {
  // LED on = setup network active; slow blink = no outdoor data yet.
  bool on = wifi_.setup_active();
  if (!on && !station_.have_outdoor()) on = (now / 1000) % 2 == 0;
  if (on != led_) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    led_ = on;
  }
}

void App::loop_once() {
  const uint32_t start = now_ms();
  const uint32_t now = start;

  crashlog::crumb(100);
  poll_radio(now);
  crashlog::crumb(101);
  poll_indoor(now);
  crashlog::crumb(102);
  poll_network(now);
  crashlog::crumb(107);
  poll_button(now);
  crashlog::crumb(108);
  station_.tick(now, time_us_64());
  crashlog::crumb(109);
  update_led(now);

  if (reboot_pending_ && reached(now, reboot_at_)) {
    // Save rain history first so nothing is lost.
    if (station_.history_dirty()) history_store_.save(&station_.history(), sizeof(wx::History));
    if (wipe_pending_) settings_store_.wipe();
    std::printf("rebooting\n");
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
  }

  crashlog::crumb(110);
  health_.report(t_radio_, radio_.progress(), now);
  health_.report(t_indoor_, indoor_.progress(), now);
  health_.report(t_net_, wifi_.progress(), now);
  const uint32_t end = now_ms();
  health_.note_loop(end - start);
  if (health_.all_ok(end)) {
    watchdog_update();
  } else {
    static bool logged = false;
    if (!logged) {
      std::printf("task stalled: %s; letting the watchdog reset\n", health_.stalled(end));
      logged = true;
    }
  }
}

// ---------------------------------------------------------------- web

wx::Station::SysInfo App::sys_info() {
  wx::Station::SysInfo s{};
  s.uptime_s = now_ms() / 1000;
  s.ip = wifi_.ip_str();
  s.hostname = settings_.hostname;
  s.setup_mode = wifi_.setup_active();
  s.wifi_reconnects = wifi_.reconnects();
  s.radio_received = radio_.stats().received;
  s.radio_rejected = radio_.rejected();
  s.radio_power_cycles = radio_.stats().power_cycles;
  s.max_loop_ms = health_.max_loop_ms();
  s.version = WX_VERSION;
  return s;
}

void App::serve_asset(const char* name, HttpResponse& r) {
  const web::Asset* a = web::find(name);
  if (a == nullptr) {
    r.status = 404;
    r.dyn_len = static_cast<size_t>(std::snprintf(r.dyn, r.dyn_cap, "Not found"));
    return;
  }
  r.content_type = a->type;
  r.gzip = true;
  r.static_body = a->data;
  r.static_len = a->len;
}

void App::serve_json_error(HttpResponse& r, int status, const char* msg) {
  r.status = status;
  r.content_type = "application/json";
  wx::JsonWriter w(r.dyn, r.dyn_cap);
  w.begin_object().key("ok").boolean(false).key("error").value(msg).end_object();
  r.dyn_len = w.size();
}

void App::handle(const wx::HttpRequest& req, uint32_t local_ip, HttpResponse& r) {
  const char* p = req.path;
  const bool get = req.method == wx::HttpMethod::kGet;
  const bool post = req.method == wx::HttpMethod::kPost;
  const uint32_t now = now_ms();

  // Setup network: anything not addressed to us (a phone's "is there
  // internet?" probe) gets redirected to the setup page, which makes the
  // phone pop it up automatically.
  if (wifi_.setup_active() && local_ip == wifi_.ap_ip()) {
    char ours[24];
    const uint32_t ip = local_ip;
    std::snprintf(ours, sizeof(ours), "%u.%u.%u.%u", static_cast<unsigned>(ip & 0xFF),
                  static_cast<unsigned>((ip >> 8) & 0xFF), static_cast<unsigned>((ip >> 16) & 0xFF),
                  static_cast<unsigned>(ip >> 24));
    if (std::strncmp(req.host, ours, std::strlen(ours)) != 0) {
      static char loc[48];
      std::snprintf(loc, sizeof(loc), "http://%s/config", ours);
      r.status = 302;
      r.location = loc;
      return;
    }
  }

  if (get && std::strcmp(p, "/") == 0) {
    char letter = station_.active_layout(time_us_64());
    const char* q = std::strstr(req.query, "layout=");
    if (q != nullptr) {
      char c = q[7];
      if (c >= 'a' && c <= 'd') c = static_cast<char>(c - 'a' + 'A');
      if (c >= 'A' && c <= 'D') letter = c;
    }
    char name[16];
    std::snprintf(name, sizeof(name), "layout_%c.html", letter - 'A' + 'a');
    serve_asset(name, r);
    return;
  }
  if (get && std::strcmp(p, "/app.js") == 0) return serve_asset("app.js", r);
  if (get && std::strcmp(p, "/config") == 0) return serve_asset("config.html", r);
  if (std::strcmp(p, "/favicon.ico") == 0) {
    r.status = 204;
    return;
  }

  if (get && std::strcmp(p, "/data.json") == 0) {
    r.content_type = "application/json";
    if (!station_.write_json(r.dyn, r.dyn_cap, now, time_us_64(), sys_info())) {
      serve_json_error(r, 500, "response too large");
      return;
    }
    r.dyn_len = std::strlen(r.dyn);
    return;
  }

  if (get && std::strcmp(p, "/api/http") == 0) {
    r.content_type = "application/json";
    int n = http::debug_json(r.dyn, r.dyn_cap);
    if (n > 1 && static_cast<size_t>(n) < r.dyn_cap) {
      r.dyn[n - 1] = ',';  // replace '}' and append the crash report
      const int m = std::snprintf(r.dyn + n, r.dyn_cap - n, "\"crash\":");
      const int k = crashlog::to_json(r.dyn + n + m, static_cast<unsigned>(r.dyn_cap - n - m));
      n += m + k;
      if (static_cast<size_t>(n) < r.dyn_cap) {
        n += std::snprintf(r.dyn + n, r.dyn_cap - n,
                           ",\"indoor\":{\"addr\":\"0x%02x\",\"chip\":\"0x%02x\",\"errors\":%lu,\"valid\":%s}",
                           indoor_.addr(), indoor_.chip_id(), (unsigned long)indoor_.errors(),
                           indoor_.reading().valid ? "true" : "false");
      }
      if (static_cast<size_t>(n) + 12 < r.dyn_cap) {
        n += std::snprintf(r.dyn + n, r.dyn_cap - n, ",\"radio\":\"");
        char line[200];
        radio_.describe(line, sizeof(line));
        for (const char* c = line; *c && static_cast<size_t>(n) + 3 < r.dyn_cap; ++c) {
          if (*c == '"' || *c == '\\') r.dyn[n++] = '\\';
          r.dyn[n++] = *c;
        }
        if (static_cast<size_t>(n) + 1 < r.dyn_cap) r.dyn[n++] = '"';
      }
      if (static_cast<size_t>(n) + 1 < r.dyn_cap) r.dyn[n++] = '}';
    }
    r.dyn_len = n > 0 ? static_cast<size_t>(n) : 0;
    return;
  }

  if (get && std::strcmp(p, "/api/i2c") == 0) {
    r.content_type = "application/json";
    uint8_t found[16];
    const int k = i2c_.scan(found, 16);
    int n = std::snprintf(r.dyn, r.dyn_cap, "{\"sda_high\":%s,\"scl_high\":%s,\"found\":[",
                          i2c_.sda_high() ? "true" : "false", i2c_.scl_high() ? "true" : "false");
    for (int i = 0; i < k && n > 0 && static_cast<size_t>(n) < r.dyn_cap; ++i)
      n += std::snprintf(r.dyn + n, r.dyn_cap - n, "%s\"0x%02x\"", i ? "," : "", found[i]);
    if (n > 0 && static_cast<size_t>(n) < r.dyn_cap) n += std::snprintf(r.dyn + n, r.dyn_cap - n, "]}");
    r.dyn_len = n > 0 && static_cast<size_t>(n) < r.dyn_cap ? static_cast<size_t>(n) : 0;
    return;
  }

  if (std::strcmp(p, "/api/config") == 0) {
    if (get) {
      r.content_type = "application/json";
      wx::JsonWriter w(r.dyn, r.dyn_cap);
      wx::write_settings_json(settings_, w);
      r.dyn_len = w.size();
      return;
    }
    if (post) {
      if (settings_.pin[0] && now - last_pin_fail_ms_ < kPinFailDelayMs) {
        return serve_json_error(r, 403, "Too fast, wait a second");
      }
      wx::Settings next = settings_;
      const wx::ConfigResult res = wx::apply_config_form(&next, req.body, req.body_len);
      if (!res.ok) {
        if (std::strcmp(res.error, "Wrong PIN") == 0) last_pin_fail_ms_ = now;
        return serve_json_error(r, 400, res.error);
      }
      settings_ = next;
      save_settings();
      station_.set_settings(settings_);
      if (res.hostname_changed) {
        wifi_.set_hostname(settings_.hostname);
        if (mdns_added_) {
          mdns_resp_rename_netif(sta_netif(), settings_.hostname);
          if (mdns_slot_ >= 0) mdns_resp_rename_service(sta_netif(), static_cast<u8_t>(mdns_slot_), settings_.hostname);
        }
      }
      if (res.location_changed) forecast_.request_now();
      if (res.wifi_changed && !kDevWifi) wifi_.set_network(settings_.wifi_ssid, settings_.wifi_pass, now);
      r.content_type = "application/json";
      wx::JsonWriter w(r.dyn, r.dyn_cap);
      w.begin_object().key("ok").boolean(true).key("wifi_changed").boolean(res.wifi_changed);
      w.key("hostname").value(settings_.hostname).end_object();
      r.dyn_len = w.size();
      return;
    }
  }

  if (std::strcmp(p, "/api/scan") == 0) {
    if (post) wifi_.start_scan();
    r.content_type = "application/json";
    wx::JsonWriter w(r.dyn, r.dyn_cap);
    w.begin_object().key("scanning").boolean(wifi_.scanning()).key("nets").begin_array();
    const ScanResult* res = nullptr;
    const int n = wifi_.scan_results(&res);
    for (int i = 0; i < n; ++i) {
      w.begin_object().key("ssid").value(res[i].ssid).key("rssi").value(int64_t{res[i].rssi});
      w.key("auth").value(int64_t{res[i].auth}).end_object();
    }
    w.end_array().end_object();
    r.dyn_len = w.size();
    return;
  }

  if (post && (std::strcmp(p, "/api/reboot") == 0 || std::strcmp(p, "/api/reset") == 0)) {
    if (!body_has_pin(settings_, req)) {
      last_pin_fail_ms_ = now;
      return serve_json_error(r, 403, "Wrong PIN");
    }
    wipe_pending_ = std::strcmp(p, "/api/reset") == 0;
    reboot_pending_ = true;
    reboot_at_ = now + 1500;  // let the response go out first
    r.content_type = "application/json";
    r.dyn_len = static_cast<size_t>(std::snprintf(r.dyn, r.dyn_cap, "{\"ok\":true}"));
    return;
  }

  if (!get && !post) {
    r.status = 405;
    return;
  }
  r.status = 404;
  r.dyn_len = static_cast<size_t>(std::snprintf(r.dyn, r.dyn_cap, "Not found"));
}

}  // namespace wxb
