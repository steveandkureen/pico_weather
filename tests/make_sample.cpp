// Prints a realistic /data.json produced by the real Station code, for the
// web dev server (tools/dev_server.py). Simulates a day of packets with some
// rain, plus the forecast fixture.
//
//   ./build/tests/wx_sample [problems]   > sample.json
#include <cstdio>
#include <cstring>
#include <string>

#include "wx/calendar.h"
#include "wx/forecast.h"
#include "wx/station.h"

#ifndef WX_FIXTURE_DIR
#define WX_FIXTURE_DIR "."
#endif

int main(int argc, char** argv) {
  const bool with_problems = argc > 1 && std::strcmp(argv[1], "problems") == 0;

  wx::Station s;
  wx::Settings st;
  wx::settings_defaults(&st);
  st.have_location = 1;
  st.lat_e5 = 4000000;
  st.lon_e5 = -10000000;
  st.have_elevation = 1;
  st.elevation_cm = 68800;
  std::strcpy(st.tz, "CST6CDT,M3.2.0,M11.1.0");
  s.set_settings(st);

  // 2026-11-12 06:00 local (12:00 UTC); simulate until 14:41 local.
  const uint32_t start_unix = static_cast<uint32_t>(wx::days_from_civil(2026, 11, 12)) * 86400u + 12u * 3600u;
  const uint64_t us0 = 1000000;
  s.clock().sync(start_unix, us0);

  std::string body;
  if (std::FILE* f = std::fopen(WX_FIXTURE_DIR "/open_meteo_11day.json", "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) body.append(buf, n);
    std::fclose(f);
  }
  wx::Forecast fc;
  if (wx::forecast_parse(body.data(), body.size(), &fc)) s.on_forecast(fc, 1000);

  uint32_t tips = 0;
  const int steps = (8 * 60 + 41) * 2;  // every 30 s
  uint32_t ms = 1000;
  uint64_t us = us0;
  for (int i = 0; i <= steps; ++i) {
    wx::OutdoorReading r;
    r.boot_id = 0x1234;
    r.seq = static_cast<uint16_t>(i);
    r.uptime_s = static_cast<uint32_t>(i * 30 + 3600);
    r.reset_reason = wx::ResetReason::kPowerOn;
    r.valid = 0x3F;
    // Temperature: 4 °C at dawn rising to 17 °C mid-afternoon.
    r.temp_c_x100 = static_cast<int16_t>(400 + (1300 * i) / steps);
    r.humidity_x100 = static_cast<uint16_t>(6800 - (3000 * i) / steps);
    r.pressure_pa = 93250;
    r.wind_pulses = static_cast<uint16_t>(60 + (i % 7) * 4);
    r.wind_interval_ms = 30000;
    r.gust_pulses_3s = 22;
    r.wind_dir_raw_deg = 135;
    if (i > 100 && i < 160 && i % 3 == 0) ++tips;  // a morning shower
    r.rain_tips = tips;
    r.battery_mv = with_problems ? 2950 : 3310;
    r.solar_mv = 6100;
    s.on_packet(r, -78, ms, us);
    s.tick(ms, us);
    ms += 30000;
    us += 30000000ull;
  }
  s.on_indoor(true, 2150, 4100, 93300, true);
  if (fc.valid) s.on_forecast(fc, ms - 20 * 60000);  // fetched 20 min ago

  wx::Station::SysInfo sys{};
  sys.uptime_s = ms / 1000;
  sys.ip = "192.168.1.50";
  sys.hostname = "weather";
  sys.version = "sample";
  sys.radio_received = static_cast<uint32_t>(steps);
  sys.max_loop_ms = 3;

  // Ask for the JSON a little after the last packet (or stale, with problems).
  const uint32_t ask_ms = with_problems ? ms + 7 * 60000 : ms - 25000;
  const uint64_t ask_us = with_problems ? us + 7ull * 60000000ull : us - 25000000ull;
  static char out[8192];
  if (!s.write_json(out, sizeof(out), ask_ms, ask_us, sys)) {
    std::fprintf(stderr, "buffer too small\n");
    return 1;
  }
  std::puts(out);
  return 0;
}
