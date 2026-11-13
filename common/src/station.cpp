#include "wx/station.h"

#include <climits>
#include <cmath>
#include <cstring>

#include "wx/calendar.h"
#include "wx/wind.h"

namespace wx {
namespace {

constexpr uint32_t kMinute = 60u * 1000u;
constexpr uint32_t kDay = 24u * 60u * kMinute;
constexpr uint32_t kStaleAmberMs = 5 * kMinute;
constexpr uint32_t kStaleRedMs = 15 * kMinute;
constexpr uint32_t kStartupGraceMs = 2 * kMinute;
constexpr uint32_t kForecastStaleMs = 3 * 60 * kMinute;
constexpr uint8_t kSensorStreak = 3;     // packets in a row before a sensor counts as failed
constexpr uint16_t kBatteryLowMv = 3000;  // LiFePO4: ~3.2 V nominal, empty below ~2.8 V
constexpr uint16_t kBatteryCritMv = 2850;
constexpr int kWeakSignalDbm = -115;

void day_str(int32_t day, char* out) {
  Date d = civil_from_days(day);
  // YYYY-MM-DD
  int y = static_cast<int>(d.year);
  out[0] = static_cast<char>('0' + (y / 1000) % 10);
  out[1] = static_cast<char>('0' + (y / 100) % 10);
  out[2] = static_cast<char>('0' + (y / 10) % 10);
  out[3] = static_cast<char>('0' + y % 10);
  out[4] = '-';
  out[5] = static_cast<char>('0' + d.month / 10);
  out[6] = static_cast<char>('0' + d.month % 10);
  out[7] = '-';
  out[8] = static_cast<char>('0' + d.day / 10);
  out[9] = static_cast<char>('0' + d.day % 10);
  out[10] = '\0';
}

const char* temp_unit_str(TempUnit u) { return u == TempUnit::kC ? "C" : "F"; }
const char* press_unit_str(PressUnit u) {
  return u == PressUnit::kHPa ? "hPa" : (u == PressUnit::kMmHg ? "mmHg" : "inHg");
}
const char* wind_unit_str(WindUnit u) {
  switch (u) {
    case WindUnit::kKmh: return "km/h";
    case WindUnit::kMps: return "m/s";
    case WindUnit::kKnots: return "kn";
    default: return "mph";
  }
}
const char* rain_unit_str(RainUnit u) { return u == RainUnit::kMm ? "mm" : "in"; }

}  // namespace

const char* problem_name(Problem p) {
  switch (p) {
    case Problem::kOutdoorNone: return "outdoor_none";
    case Problem::kOutdoorStale: return "outdoor_stale";
    case Problem::kRadio: return "radio";
    case Problem::kSensorTempHum: return "sensor_th";
    case Problem::kSensorPressure: return "sensor_p";
    case Problem::kSensorWind: return "sensor_wind";
    case Problem::kSensorWindDir: return "sensor_dir";
    case Problem::kBatteryLow: return "battery";
    case Problem::kOutdoorRestarts: return "outdoor_restarts";
    case Problem::kBaseRestarted: return "base_restarted";
    case Problem::kIndoorSensor: return "indoor_sensor";
    case Problem::kClock: return "clock";
    case Problem::kForecast: return "forecast";
    case Problem::kNoLocation: return "no_location";
    case Problem::kFlash: return "flash";
    case Problem::kWeakSignal: return "weak_signal";
  }
  return "unknown";
}

Station::Station() {
  settings_defaults(&settings_);
  tz_parse(settings_.tz, &tz_);
  std::memset(&fc_, 0, sizeof(fc_));
}

void Station::set_settings(const Settings& s) {
  settings_ = s;
  settings_sanitize(&settings_);
  if (!tz_parse(settings_.tz, &tz_)) tz_ = TimeZone{};
}

void Station::load_history(const History& h) {
  if (!ledger_.load(h.rain)) {
    ledger_.reset();
  }
  ext_ = h.extremes;
  history_dirty_ = false;
}

const History& Station::history() {
  snapshot_.rain = ledger_.state();
  snapshot_.extremes = ext_;
  return snapshot_;
}

int32_t Station::today(uint64_t mono_us) const {
  if (!clock_.is_set()) return INT32_MIN;
  return to_local(tz_, clock_.now(mono_us)).day;
}

void Station::roll_day(int32_t day) {
  if (ext_.have && ext_.day != day) {
    ext_.have = 0;
    history_dirty_ = true;
  }
}

void Station::update_extremes(int16_t t, int32_t day) {
  roll_day(day);
  if (!ext_.have) {
    ext_.day = day;
    ext_.hi_c_x100 = t;
    ext_.lo_c_x100 = t;
    ext_.have = 1;
    history_dirty_ = true;
    return;
  }
  if (t > ext_.hi_c_x100) {
    ext_.hi_c_x100 = t;
    history_dirty_ = true;
  }
  if (t < ext_.lo_c_x100) {
    ext_.lo_c_x100 = t;
    history_dirty_ = true;
  }
}

void Station::tick(uint32_t mono_ms, uint64_t mono_us) {
  (void)mono_ms;
  const int32_t d = today(mono_us);
  if (d == INT32_MIN) return;
  if (pending_tips_ > 0) {
    ledger_.credit(d, pending_tips_);
    pending_tips_ = 0;
    history_dirty_ = true;
  }
  if (ledger_.has_day() && d > ledger_.newest_day()) {
    ledger_.credit(d, 0);  // roll the ledger to the new day
    history_dirty_ = true;
  }
  roll_day(d);
}

void Station::on_packet(const OutdoorReading& r, int16_t rssi_dbm, uint32_t mono_ms, uint64_t mono_us) {
  const bool new_boot = !have_out_ || r.boot_id != last_boot_id_;
  if (new_boot && r.reset_reason == ResetReason::kWatchdog && r.uptime_s < 86400) {
    wd_events_ms_[wd_count_ % 8] = mono_ms - r.uptime_s * 1000u;
    ++wd_count_;
  }
  last_boot_id_ = r.boot_id;

  // Rain.
  const uint32_t delta = ledger_.take_delta(r.boot_id, r.rain_tips);
  history_dirty_ = true;  // baseline changed
  const int32_t d = today(mono_us);
  if (d != INT32_MIN) {
    ledger_.credit(d, pending_tips_ + delta);
    pending_tips_ = 0;
  } else {
    pending_tips_ += delta;
  }

  // Sensor health streaks.
  auto streak = [](uint8_t& s, bool ok) { s = ok ? 0 : static_cast<uint8_t>(s < 255 ? s + 1 : s); };
  streak(streak_th_, (r.valid & kValidTempHumidity) != 0);
  streak(streak_p_, (r.valid & kValidPressure) != 0);
  streak(streak_wind_, (r.valid & kValidWind) != 0);
  streak(streak_dir_, (r.valid & kValidWindDir) != 0);

  if ((r.valid & kValidTempHumidity) && d != INT32_MIN) update_extremes(r.temp_c_x100, d);

  // Signal strength average.
  if (rssi_dbm != 0) {
    if (rssi_avg_x16_ == 0) {
      rssi_avg_x16_ = rssi_dbm * 16;
    } else {
      rssi_avg_x16_ += (rssi_dbm * 16 - rssi_avg_x16_) / 8;
    }
    rssi_last_ = rssi_dbm;
  }

  out_ = r;
  out_ms_ = mono_ms;
  have_out_ = true;
}

void Station::on_indoor(bool valid, int16_t t, uint16_t h, uint32_t p, bool has_h) {
  if (!valid) {
    if (in_fail_streak_ < 255) ++in_fail_streak_;
    if (in_fail_streak_ >= kSensorStreak) in_valid_ = false;
    return;
  }
  in_fail_streak_ = 0;
  in_valid_ = true;
  in_has_h_ = has_h;
  in_t_ = t;
  in_h_ = h;
  in_p_ = p;
}

void Station::on_forecast(const Forecast& f, uint32_t mono_ms) {
  if (!f.valid) return;
  fc_ = f;
  fc_ms_ = mono_ms;
}

void Station::set_radio_ok(bool ok, uint32_t mono_ms) {
  if (ok) {
    radio_ok_ = true;
  } else if (radio_ok_) {
    radio_ok_ = false;
    radio_bad_since_ms_ = mono_ms;
  }
}

uint32_t Station::sea_level_pa(uint32_t station_pa, int32_t elevation_cm, int16_t temp_c_x100) {
  // Hypsometric formula, as used by most weather stations.
  const float h = static_cast<float>(elevation_cm) / 100.0f;
  const float t = static_cast<float>(temp_c_x100) / 100.0f;
  const float ratio = 1.0f - (0.0065f * h) / (t + 0.0065f * h + 273.15f);
  if (ratio <= 0.1f) return station_pa;
  const float p0 = static_cast<float>(station_pa) * std::pow(ratio, -5.257f);
  return static_cast<uint32_t>(p0 + 0.5f);
}

int Station::problems(uint32_t mono_ms, uint64_t mono_us, ProblemEntry* out, int cap) const {
  int n = 0;
  auto add = [&](Problem p, uint8_t level, int32_t v) {
    if (n < cap) out[n++] = ProblemEntry{p, level, v};
  };
  const uint32_t up_ms = mono_ms;  // monotonic clock starts at boot

  if (flash_error_) add(Problem::kFlash, 2, 0);
  if (!radio_ok_ && mono_ms - radio_bad_since_ms_ > kMinute) add(Problem::kRadio, 2, 0);

  if (!have_out_) {
    if (up_ms > kStartupGraceMs) add(Problem::kOutdoorNone, 2, 0);
  } else {
    const uint32_t age = mono_ms - out_ms_;
    if (age >= kStaleRedMs) {
      add(Problem::kOutdoorStale, 2, static_cast<int32_t>(age / kMinute));
    } else if (age >= kStaleAmberMs) {
      add(Problem::kOutdoorStale, 1, static_cast<int32_t>(age / kMinute));
    }
    if ((out_.valid & kValidBattery) && out_.battery_mv > 0) {
      if (out_.battery_mv < kBatteryCritMv) {
        add(Problem::kBatteryLow, 2, out_.battery_mv);
      } else if (out_.battery_mv < kBatteryLowMv) {
        add(Problem::kBatteryLow, 1, out_.battery_mv);
      }
    }
    if (streak_th_ >= kSensorStreak) add(Problem::kSensorTempHum, 1, 0);
    if (streak_p_ >= kSensorStreak) add(Problem::kSensorPressure, 1, 0);
    if (streak_wind_ >= kSensorStreak) add(Problem::kSensorWind, 1, 0);
    if (streak_dir_ >= kSensorStreak) add(Problem::kSensorWindDir, 1, 0);
    if (rssi_avg_x16_ != 0 && rssi_avg_x16_ / 16 < kWeakSignalDbm) {
      add(Problem::kWeakSignal, 1, rssi_avg_x16_ / 16);
    }
  }

  int wd = 0;
  const uint8_t stored = wd_count_ < 8 ? wd_count_ : 8;
  for (uint8_t i = 0; i < stored; ++i) {
    if (mono_ms - wd_events_ms_[i] < kDay) ++wd;
  }
  if (wd > 0) add(Problem::kOutdoorRestarts, 1, wd);
  if (base_wd_ && up_ms < kDay) add(Problem::kBaseRestarted, 1, 0);
  if (!in_valid_ && in_fail_streak_ >= kSensorStreak) add(Problem::kIndoorSensor, 1, 0);
  if (!clock_.is_set() && up_ms > kStartupGraceMs) add(Problem::kClock, 1, 0);

  if (!settings_.have_location) {
    add(Problem::kNoLocation, 1, 0);
  } else if (fc_.valid ? (mono_ms - fc_ms_ > kForecastStaleMs) : (up_ms > 10 * kMinute)) {
    add(Problem::kForecast, 1, fc_.valid ? static_cast<int32_t>((mono_ms - fc_ms_) / kMinute) : 0);
  }
  (void)mono_us;

  // Worst first.
  for (int i = 1; i < n; ++i) {
    ProblemEntry e = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].level < e.level) {
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = e;
  }
  return n;
}

char Station::active_layout(uint64_t mono_us) const {
  if (settings_.layout != kLayoutAuto) return static_cast<char>(settings_.layout);
  int32_t minute = -1;
  int32_t day = INT32_MIN;
  if (clock_.is_set()) {
    LocalTime lt = to_local(tz_, clock_.now(mono_us));
    minute = lt.minute_of_day;
    day = lt.day;
  }
  if (minute < 0) return static_cast<char>(settings_.day_layout);  // no clock yet: show the day layout
  int32_t sunrise = 7 * 60, sunset = 19 * 60;
  if (fc_.valid) {
    for (int i = 0; i < fc_.ndays; ++i) {
      if (fc_.days[i].day == day && fc_.days[i].sunrise_min >= 0 && fc_.days[i].sunset_min >= 0) {
        sunrise = fc_.days[i].sunrise_min;
        sunset = fc_.days[i].sunset_min;
        break;
      }
    }
  }
  const bool daytime = minute >= sunrise && minute < sunset;
  return static_cast<char>(daytime ? settings_.day_layout : settings_.night_layout);
}

bool Station::write_json(char* buf, size_t cap, uint32_t mono_ms, uint64_t mono_us,
                         const SysInfo& sys) const {
  JsonWriter w(buf, cap);
  w.begin_object();
  w.key("v").value(int64_t{1});

  const bool clock_set = clock_.is_set();
  const int64_t now = clock_.now(mono_us);
  w.key("clock").boolean(clock_set);
  w.key("now").value(clock_set ? now : int64_t{0});
  w.key("tz_off").value(int64_t{clock_set ? tz_offset(tz_, now) : tz_.std_offset_s});

  const char layout[2] = {active_layout(mono_us), '\0'};
  w.key("layout").value(layout);
  const char setting[2] = {static_cast<char>(settings_.layout), '\0'};
  w.key("layout_setting").value(settings_.layout == kLayoutAuto ? "auto" : setting);

  w.key("units").begin_object();
  w.key("t").value(temp_unit_str(settings_.temp_unit));
  w.key("p").value(press_unit_str(settings_.press_unit));
  w.key("w").value(wind_unit_str(settings_.wind_unit));
  w.key("r").value(rain_unit_str(settings_.rain_unit));
  w.end_object();

  // Outdoor.
  w.key("out");
  if (!have_out_) {
    w.null();
  } else {
    w.begin_object();
    w.key("age_s").value(int64_t{(mono_ms - out_ms_) / 1000});
    const bool th = (out_.valid & kValidTempHumidity) != 0;
    const bool pv = (out_.valid & kValidPressure) != 0;
    w.key("t");
    th ? w.value(int64_t{out_.temp_c_x100}) : w.null();
    w.key("h");
    th ? w.value(int64_t{out_.humidity_x100}) : w.null();
    w.key("p");
    pv ? w.value(int64_t{out_.pressure_pa}) : w.null();
    w.key("p_sea");
    if (pv && settings_.have_elevation) {
      const int16_t t = th ? out_.temp_c_x100 : 1500;
      w.value(int64_t{sea_level_pa(out_.pressure_pa, settings_.elevation_cm, t)});
    } else {
      w.null();
    }
    const bool wind = (out_.valid & kValidWind) != 0;
    w.key("wind");
    wind ? w.value(int64_t{wind_mps_x100(out_.wind_pulses, out_.wind_interval_ms, settings_.wind_mmps_per_hz)})
         : w.null();
    w.key("gust");
    wind ? w.value(int64_t{wind_mps_x100(out_.gust_pulses_3s, 3000, settings_.wind_mmps_per_hz)}) : w.null();
    w.key("dir");
    if ((out_.valid & kValidWindDir) && out_.wind_dir_raw_deg != kNoWindDir) {
      w.value(int64_t{vane_true_deg(out_.wind_dir_raw_deg, settings_.vane_offset_deg)});
    } else {
      w.null();
    }
    w.key("hi");
    ext_.have ? w.value(int64_t{ext_.hi_c_x100}) : w.null();
    w.key("lo");
    ext_.have ? w.value(int64_t{ext_.lo_c_x100}) : w.null();
    w.key("batt_mv");
    (out_.valid & kValidBattery) ? w.value(int64_t{out_.battery_mv}) : w.null();
    w.key("solar_mv");
    (out_.valid & kValidSolar) ? w.value(int64_t{out_.solar_mv}) : w.null();
    w.key("rssi").value(int64_t{rssi_last_});
    w.key("uptime_s").value(int64_t{out_.uptime_s});
    w.key("seq").value(int64_t{out_.seq});
    w.end_object();
  }

  // Indoor.
  w.key("in");
  if (!in_valid_) {
    w.null();
  } else {
    w.begin_object();
    w.key("t").value(int64_t{in_t_});
    w.key("h");
    in_has_h_ ? w.value(int64_t{in_h_}) : w.null();
    w.key("p").value(int64_t{in_p_});
    w.end_object();
  }

  // Rain (micrometres).
  const int32_t d = today(mono_us);
  const uint32_t um = settings_.rain_um_per_tip;
  w.key("rain").begin_object();
  if (d != INT32_MIN) {
    w.key("today_um").value(int64_t{static_cast<int64_t>(ledger_.today_tips(d)) * um});
    w.key("month_um").value(int64_t{static_cast<int64_t>(ledger_.month_tips(d)) * um});
    w.key("year_um").value(int64_t{static_cast<int64_t>(ledger_.year_tips(d)) * um});
    w.key("days_um").begin_array();
    for (int32_t i = 29; i >= 0; --i) w.value(int64_t{static_cast<int64_t>(ledger_.day_tips(d - i)) * um});
    w.end_array();
  } else {
    w.key("today_um").null();
    w.key("month_um").null();
    w.key("year_um").null();
  }
  w.key("pending_um").value(int64_t{static_cast<int64_t>(pending_tips_) * um});
  w.end_object();

  // Forecast.
  w.key("fc");
  if (!fc_.valid) {
    w.null();
  } else {
    w.begin_object();
    w.key("age_s").value(int64_t{(mono_ms - fc_ms_) / 1000});
    w.key("code").value(int64_t{fc_.current_code});
    w.key("days").begin_array();
    char ds[12];
    for (int i = 0; i < fc_.ndays; ++i) {
      const ForecastDay& fd = fc_.days[i];
      // Skip days already past (forecast fetched yesterday).
      if (d != INT32_MIN && fd.day < d) continue;
      day_str(fd.day, ds);
      w.begin_object();
      w.key("d").value(ds);
      w.key("c").value(int64_t{fd.code});
      w.key("hi").value(int64_t{fd.tmax_c_x10});
      w.key("lo").value(int64_t{fd.tmin_c_x10});
      w.key("pp").value(int64_t{fd.precip_prob});
      w.key("pr").value(int64_t{fd.precip_mm_x10});
      w.key("w").value(int64_t{fd.wind_max_mps_x10});
      w.key("sr").value(int64_t{fd.sunrise_min});
      w.key("ss").value(int64_t{fd.sunset_min});
      w.end_object();
    }
    w.end_array();
    w.end_object();
  }

  // Problems.
  ProblemEntry probs[kMaxProblems];
  const int np = problems(mono_ms, mono_us, probs, kMaxProblems);
  w.key("problems").begin_array();
  for (int i = 0; i < np; ++i) {
    w.begin_object();
    w.key("c").value(problem_name(probs[i].code));
    w.key("l").value(int64_t{probs[i].level});
    w.key("x").value(int64_t{probs[i].value});
    w.end_object();
  }
  w.end_array();

  // System (for /config).
  w.key("sys").begin_object();
  w.key("version").value(sys.version ? sys.version : "");
  w.key("uptime_s").value(int64_t{sys.uptime_s});
  w.key("ip").value(sys.ip ? sys.ip : "");
  w.key("host").value(sys.hostname ? sys.hostname : "");
  w.key("setup").boolean(sys.setup_mode);
  w.key("wifi_reconnects").value(int64_t{sys.wifi_reconnects});
  w.key("rx").value(int64_t{sys.radio_received});
  w.key("rx_bad").value(int64_t{sys.radio_rejected});
  w.key("radio_cycles").value(int64_t{sys.radio_power_cycles});
  w.key("max_loop_ms").value(int64_t{sys.max_loop_ms});
  w.key("out_max_loop_ms").value(int64_t{have_out_ ? out_.max_loop_ms : 0});
  w.key("out_sensor_err").value(int64_t{have_out_ ? out_.sensor_errors : 0});
  w.key("rssi_avg").value(int64_t{rssi_avg_x16_ / 16});
  w.end_object();

  w.end_object();
  return w.ok();
}

}  // namespace wx
