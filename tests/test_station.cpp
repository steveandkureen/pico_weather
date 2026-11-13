// The base station model: packets → rain/extremes/problems/JSON.
#include <cstring>
#include <string>

#include "test_util.h"
#include "wx/calendar.h"
#include "wx/json.h"
#include "wx/station.h"

namespace {

constexpr uint64_t kSec = 1000000;

// 2026-11-12 18:00 UTC = 11:00 MST.
constexpr uint32_t kNoonish = 1794506400u;

wx::OutdoorReading pkt(uint16_t boot, uint32_t tips, int16_t t) {
  wx::OutdoorReading r;
  r.boot_id = boot;
  r.rain_tips = tips;
  r.temp_c_x100 = t;
  r.humidity_x100 = 4000;
  r.pressure_pa = 84000;
  r.valid = wx::kValidTempHumidity | wx::kValidPressure | wx::kValidWind | wx::kValidWindDir |
            wx::kValidBattery;
  r.wind_pulses = 30;
  r.wind_interval_ms = 30000;
  r.gust_pulses_3s = 9;
  r.wind_dir_raw_deg = 100;
  r.battery_mv = 3300;
  r.reset_reason = wx::ResetReason::kPowerOn;
  r.uptime_s = 1000;
  return r;
}

bool has_problem(const wx::Station& s, uint32_t ms, uint64_t us, wx::Problem p, uint8_t* level = nullptr) {
  wx::ProblemEntry e[16];
  int n = s.problems(ms, us, e, 16);
  for (int i = 0; i < n; ++i) {
    if (e[i].code == p) {
      if (level) *level = e[i].level;
      return true;
    }
  }
  return false;
}

void test_rain_held_until_clock_set() {
  wx::Station s;
  s.on_packet(pkt(1, 0, 1000), -80, 1000, 1 * kSec);
  s.on_packet(pkt(1, 5, 1000), -80, 31000, 31 * kSec);
  CHECK_EQ(s.pending_tips(), 5u);
  s.clock().sync(kNoonish, 40 * kSec);
  s.tick(40000, 40 * kSec);
  CHECK_EQ(s.pending_tips(), 0u);
  wx::History h = s.history();
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  wx::RainLedger l;
  l.load(h.rain);
  CHECK_EQ(l.today_tips(day), 5u);
}

void test_extremes_and_day_rollover() {
  wx::Station s;
  s.clock().sync(kNoonish, 0);
  s.on_packet(pkt(1, 0, 1500), -70, 1000, 1 * kSec);
  s.on_packet(pkt(1, 0, 2500), -70, 2000, 2 * kSec);
  s.on_packet(pkt(1, 0, -300), -70, 3000, 3 * kSec);
  wx::History h = s.history();
  CHECK(h.extremes.have);
  CHECK_EQ(h.extremes.hi_c_x100, 2500);
  CHECK_EQ(h.extremes.lo_c_x100, -300);
  // 14 hours later it's the next local day: extremes reset on the next reading.
  const uint64_t later = 14 * 3600 * kSec;
  s.tick(50000000, later);
  s.on_packet(pkt(1, 0, 800), -70, 50001000, later + kSec);
  h = s.history();
  CHECK_EQ(h.extremes.hi_c_x100, 800);
  CHECK_EQ(h.extremes.lo_c_x100, 800);
  CHECK_EQ(h.extremes.day, wx::days_from_civil(2026, 11, 13));
}

void test_problems() {
  wx::Station s;
  // Fresh boot: nothing reported during the grace period.
  CHECK(!has_problem(s, 1000, kSec, wx::Problem::kOutdoorNone));
  CHECK(has_problem(s, 200000, 200 * kSec, wx::Problem::kOutdoorNone));
  CHECK(has_problem(s, 200000, 200 * kSec, wx::Problem::kClock));
  CHECK(has_problem(s, 200000, 200 * kSec, wx::Problem::kNoLocation));

  s.on_packet(pkt(1, 0, 1000), -80, 200000, 200 * kSec);
  CHECK(!has_problem(s, 210000, 210 * kSec, wx::Problem::kOutdoorNone));
  uint8_t level = 0;
  CHECK(has_problem(s, 200000 + 6 * 60000, 0, wx::Problem::kOutdoorStale, &level));
  CHECK_EQ(level, 1);
  CHECK(has_problem(s, 200000 + 16 * 60000, 0, wx::Problem::kOutdoorStale, &level));
  CHECK_EQ(level, 2);

  // Sensor failing 3 packets in a row.
  wx::OutdoorReading bad = pkt(1, 0, 1000);
  bad.valid &= static_cast<uint8_t>(~wx::kValidTempHumidity);
  s.on_packet(bad, -80, 230000, 0);
  s.on_packet(bad, -80, 260000, 0);
  CHECK(!has_problem(s, 260000, 0, wx::Problem::kSensorTempHum));
  s.on_packet(bad, -80, 290000, 0);
  CHECK(has_problem(s, 290000, 0, wx::Problem::kSensorTempHum));
  s.on_packet(pkt(1, 0, 1000), -80, 320000, 0);
  CHECK(!has_problem(s, 320000, 0, wx::Problem::kSensorTempHum));

  // Low battery.
  wx::OutdoorReading low = pkt(1, 0, 1000);
  low.battery_mv = 2900;
  s.on_packet(low, -80, 350000, 0);
  CHECK(has_problem(s, 350000, 0, wx::Problem::kBatteryLow, &level));
  CHECK_EQ(level, 1);

  // Outdoor watchdog restart shows for a day.
  wx::OutdoorReading wd = pkt(2, 0, 1000);
  wd.reset_reason = wx::ResetReason::kWatchdog;
  wd.uptime_s = 30;
  s.on_packet(wd, -80, 400000, 0);
  CHECK(has_problem(s, 400000, 0, wx::Problem::kOutdoorRestarts));
  CHECK(!has_problem(s, 400000 + 25 * 3600 * 1000u, 0, wx::Problem::kOutdoorRestarts));

  // Worst problems come first.
  wx::ProblemEntry e[16];
  int n = s.problems(400000 + 20 * 60000, 0, e, 16);
  CHECK(n > 1);
  for (int i = 1; i < n; ++i) CHECK(e[i - 1].level >= e[i].level);
}

void test_layout_auto() {
  wx::Station s;
  wx::Settings st;
  wx::settings_defaults(&st);
  s.set_settings(st);
  // No clock: day layout.
  CHECK_EQ(s.active_layout(0), 'B');
  // 11:00 local → day.
  s.clock().sync(kNoonish, 0);
  CHECK_EQ(s.active_layout(0), 'B');
  // 21:00 local → night.
  CHECK_EQ(s.active_layout(10 * 3600 * kSec), 'D');
  // Forecast sunset at 10:30 makes 11:00 "night".
  wx::Forecast f{};
  f.valid = true;
  f.ndays = 1;
  f.days[0].day = wx::days_from_civil(2026, 11, 12);
  f.days[0].sunrise_min = 6 * 60;
  f.days[0].sunset_min = 10 * 60 + 30;
  s.on_forecast(f, 0);
  CHECK_EQ(s.active_layout(0), 'D');
  // Fixed layout ignores time.
  st.layout = 'C';
  s.set_settings(st);
  CHECK_EQ(s.active_layout(0), 'C');
}

void test_sea_level_pressure() {
  // 840 hPa at 1609 m (Denver): ≈1013 hPa at sea level with 15 °C at the
  // station, ≈1020 hPa with the standard-atmosphere 4.5 °C.
  const uint32_t warm = wx::Station::sea_level_pa(84000, 160900, 1500);
  CHECK(warm > 101000 && warm < 101600);
  const uint32_t std_atm = wx::Station::sea_level_pa(84000, 160900, 454);
  CHECK(std_atm > 101800 && std_atm < 102300);
  CHECK_EQ(wx::Station::sea_level_pa(101325, 0, 1500), 101325u);
}

struct JsonCheck {
  bool saw_today = false;
  bool saw_layout = false;
  bool saw_problem = false;
  int64_t wind = -1;
  int64_t dir = -1;
};

void check_cb(void* u, const wx::JsonSlice* path, int depth, int, const wx::JsonValue& v) {
  JsonCheck* c = static_cast<JsonCheck*>(u);
  if (depth == 2 && path[0].equals("rain") && path[1].equals("today_um")) c->saw_today = true;
  if (depth == 1 && path[0].equals("layout")) c->saw_layout = true;
  if (depth == 2 && path[0].equals("problems") && path[1].equals("c")) c->saw_problem = true;
  if (depth == 2 && path[0].equals("out") && path[1].equals("wind")) c->wind = v.milli / 1000;
  if (depth == 2 && path[0].equals("out") && path[1].equals("dir")) c->dir = v.milli / 1000;
}

void test_json_output_is_valid() {
  wx::Station s;
  wx::Settings st;
  wx::settings_defaults(&st);
  st.vane_offset_deg = 20;
  s.set_settings(st);
  s.clock().sync(kNoonish, 0);
  s.on_packet(pkt(1, 0, 1000), -80, 1000, kSec);
  s.on_packet(pkt(1, 3, 1000), -80, 31000, 31 * kSec);
  wx::Forecast f{};
  f.valid = true;
  f.ndays = 11;
  for (int i = 0; i < 11; ++i) {
    f.days[i].day = wx::days_from_civil(2026, 11, 12) + i;
    f.days[i].sunrise_min = 400;
    f.days[i].sunset_min = 1000;
  }
  s.on_forecast(f, 31000);

  static char buf[6144];
  wx::Station::SysInfo sys{};
  sys.ip = "192.168.1.50";
  sys.hostname = "weather";
  sys.version = "test";
  CHECK(s.write_json(buf, sizeof(buf), 400000, 40 * kSec, sys));
  JsonCheck c;
  CHECK(wx::json_walk(buf, std::strlen(buf), &check_cb, &c));
  CHECK(c.saw_today);
  CHECK(c.saw_layout);
  CHECK(c.saw_problem);  // no location → a problem is listed
  CHECK_EQ(c.wind, 66);   // 1 Hz × 0.667 m/s
  CHECK_EQ(c.dir, 120);   // 100 raw + 20 offset
  CHECK(std::strlen(buf) < 4000);  // leaves headroom in the 6 KB response buffer

  char tiny[64];
  CHECK(!s.write_json(tiny, sizeof(tiny), 400000, 40 * kSec, sys));
}

}  // namespace

void run_station_tests() {
  test_rain_held_until_clock_set();
  test_extremes_and_day_rollover();
  test_problems();
  test_layout_auto();
  test_sea_level_pressure();
  test_json_output_is_valid();
}
