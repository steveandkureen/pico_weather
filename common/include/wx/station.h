// The base station's model of the world: latest readings, rain history,
// today's high/low, forecast and the list of active problems. Pure logic (no
// SDK), driven by the firmware's main loop and tested on the PC.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/clock.h"
#include "wx/forecast.h"
#include "wx/json.h"
#include "wx/packet.h"
#include "wx/rain_ledger.h"
#include "wx/settings.h"
#include "wx/timezone.h"

namespace wx {

// Today's outdoor high/low, saved with the rain history.
struct DailyExtremes {
  int32_t day;
  int16_t hi_c_x100;
  int16_t lo_c_x100;
  uint8_t have;
  uint8_t pad[3];
};

// Everything that is saved to flash as the "history" record.
struct History {
  RainLedger::State rain;
  DailyExtremes extremes;
};

enum class Problem : uint8_t {
  kOutdoorNone,      // nothing heard since boot
  kOutdoorStale,     // value = minutes since last packet
  kRadio,            // LoRa module not responding
  kSensorTempHum,
  kSensorPressure,
  kSensorWind,
  kSensorWindDir,
  kBatteryLow,       // value = mV
  kOutdoorRestarts,  // value = count in last 24 h
  kBaseRestarted,
  kIndoorSensor,
  kClock,
  kForecast,         // value = minutes since last good forecast (0 = never)
  kNoLocation,
  kFlash,
  kWeakSignal,       // value = average dBm (negative)
};

struct ProblemEntry {
  Problem code;
  uint8_t level;  // 1 = amber, 2 = red
  int32_t value;
};

const char* problem_name(Problem p);

class Station {
 public:
  static constexpr int kMaxProblems = 16;

  Station();

  // --- inputs ---------------------------------------------------------
  void set_settings(const Settings& s);
  const Settings& settings() const { return settings_; }
  const TimeZone& tz() const { return tz_; }

  void load_history(const History& h);
  const History& history();  // snapshot for saving
  bool history_dirty() const { return history_dirty_; }
  void history_saved(bool ok) {
    if (ok) history_dirty_ = false;
    flash_error_ = !ok;
  }
  void settings_save_failed() { flash_error_ = true; }

  Clock& clock() { return clock_; }

  // A packet arrived. mono_ms = monotonic milliseconds, mono_us for the clock.
  void on_packet(const OutdoorReading& r, int16_t rssi_dbm, uint32_t mono_ms, uint64_t mono_us);

  // Indoor sensor result (valid=false if the sensor is failing).
  void on_indoor(bool valid, int16_t temp_c_x100, uint16_t humidity_x100, uint32_t pressure_pa,
                 bool has_humidity);

  void on_forecast(const Forecast& f, uint32_t mono_ms);
  void on_forecast_failed() {}

  // Called every loop: rolls the day over, credits rain held while the clock
  // was unset, etc.
  void tick(uint32_t mono_ms, uint64_t mono_us);

  void set_radio_ok(bool ok, uint32_t mono_ms);
  void set_base_restarted_by_watchdog(bool b) { base_wd_ = b; }

  // --- outputs ---------------------------------------------------------
  int problems(uint32_t mono_ms, uint64_t mono_us, ProblemEntry* out, int cap) const;

  // 'A'..'D', resolving Auto by sunrise/sunset.
  char active_layout(uint64_t mono_us) const;

  struct SysInfo {
    uint32_t uptime_s;
    const char* ip;
    const char* hostname;
    bool setup_mode;
    uint32_t wifi_reconnects;
    uint32_t radio_received;
    uint32_t radio_rejected;
    uint32_t radio_power_cycles;
    uint32_t max_loop_ms;
    uint32_t free_heap;  // informational
    const char* version;
  };

  // Writes /data.json. Returns false if the buffer was too small.
  bool write_json(char* buf, size_t cap, uint32_t mono_ms, uint64_t mono_us, const SysInfo& sys) const;

  // Helpers exposed for tests.
  static uint32_t sea_level_pa(uint32_t station_pa, int32_t elevation_cm, int16_t temp_c_x100);
  int32_t today(uint64_t mono_us) const;  // local day, or INT32_MIN if clock unset

  bool have_outdoor() const { return have_out_; }
  const Forecast& forecast() const { return fc_; }
  uint32_t pending_tips() const { return pending_tips_; }

 private:
  void update_extremes(int16_t t, int32_t day);
  void roll_day(int32_t day);

  Settings settings_;
  TimeZone tz_;
  Clock clock_;
  RainLedger ledger_;
  DailyExtremes ext_{};
  History snapshot_{};
  bool history_dirty_ = false;
  bool flash_error_ = false;
  uint32_t pending_tips_ = 0;

  // Outdoor
  bool have_out_ = false;
  OutdoorReading out_{};
  uint32_t out_ms_ = 0;
  int16_t rssi_last_ = 0;
  int32_t rssi_avg_x16_ = 0;  // exponential average ×16
  uint8_t streak_th_ = 0, streak_p_ = 0, streak_wind_ = 0, streak_dir_ = 0;
  uint32_t wd_events_ms_[8] = {};
  uint8_t wd_count_ = 0;
  uint16_t last_boot_id_ = 0;

  // Indoor
  bool in_valid_ = false;
  bool in_has_h_ = false;
  int16_t in_t_ = 0;
  uint16_t in_h_ = 0;
  uint32_t in_p_ = 0;
  uint8_t in_fail_streak_ = 0;

  // Forecast
  Forecast fc_{};
  uint32_t fc_ms_ = 0;

  // Radio / base
  bool radio_ok_ = true;
  uint32_t radio_bad_since_ms_ = 0;
  bool base_wd_ = false;
};

}  // namespace wx
