#include "wx/settings.h"

#include <cstring>

namespace wx {
namespace {

bool layout_letter_ok(uint8_t l) { return l >= 'A' && l <= 'D'; }

void terminate(char* s, size_t cap) { s[cap - 1] = '\0'; }

}  // namespace

void copy_str(char* dst, size_t cap, const char* src, size_t len) {
  if (cap == 0) return;
  size_t n = len < cap - 1 ? len : cap - 1;
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}

void settings_defaults(Settings* s) {
  std::memset(s, 0, sizeof(*s));
  s->version = 1;
  std::strcpy(s->hostname, "weather");
  // Home location (ZIP 85747, Tucson): Arizona time, no daylight saving.
  // All of these can be changed on /config.
  std::strcpy(s->tz, "MST7");
  s->have_location = 1;
  s->lat_e5 = 3213888;    // 32.138884
  s->lon_e5 = -11076765;  // -110.767652
  s->have_elevation = 1;
  s->elevation_cm = 88500;  // 885 m / 2904 ft at those coordinates
  s->temp_unit = TempUnit::kF;
  s->press_unit = PressUnit::kInHg;
  s->wind_unit = WindUnit::kMph;
  s->rain_unit = RainUnit::kIn;
  s->layout = kLayoutAuto;
  s->day_layout = 'B';
  s->night_layout = 'D';
  s->rain_um_per_tip = 279;    // 0.011 in, a common tipping-bucket size; calibrate
  s->wind_mmps_per_hz = 667;   // 2.4 km/h per Hz, a common cup anemometer; calibrate
  s->vane_offset_deg = 0;
}

bool hostname_valid(const char* h) {
  size_t n = std::strlen(h);
  if (n == 0 || n > 31) return false;
  if (h[0] == '-' || h[n - 1] == '-') return false;
  for (size_t i = 0; i < n; ++i) {
    char c = h[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool pin_valid(const char* p) {
  size_t n = std::strlen(p);
  if (n == 0) return true;
  if (n < 4 || n > 8) return false;
  for (size_t i = 0; i < n; ++i) {
    if (p[i] < '0' || p[i] > '9') return false;
  }
  return true;
}

void settings_sanitize(Settings* s) {
  Settings d;
  settings_defaults(&d);

  terminate(s->wifi_ssid, sizeof(s->wifi_ssid));
  terminate(s->wifi_pass, sizeof(s->wifi_pass));
  terminate(s->hostname, sizeof(s->hostname));
  terminate(s->tz, sizeof(s->tz));
  terminate(s->pin, sizeof(s->pin));

  if (!hostname_valid(s->hostname)) std::strcpy(s->hostname, d.hostname);
  if (s->tz[0] == '\0') std::strcpy(s->tz, d.tz);
  if (!pin_valid(s->pin)) s->pin[0] = '\0';

  if (s->lat_e5 < -9000000 || s->lat_e5 > 9000000 || s->lon_e5 < -18000000 ||
      s->lon_e5 > 18000000) {
    s->have_location = 0;
  }
  s->have_location = s->have_location ? 1 : 0;
  if (s->elevation_cm < -50000 || s->elevation_cm > 900000) s->have_elevation = 0;
  s->have_elevation = s->have_elevation ? 1 : 0;

  if (static_cast<uint8_t>(s->temp_unit) > 1) s->temp_unit = d.temp_unit;
  if (static_cast<uint8_t>(s->press_unit) > 2) s->press_unit = d.press_unit;
  if (static_cast<uint8_t>(s->wind_unit) > 3) s->wind_unit = d.wind_unit;
  if (static_cast<uint8_t>(s->rain_unit) > 1) s->rain_unit = d.rain_unit;

  if (s->layout != kLayoutAuto && !layout_letter_ok(s->layout)) s->layout = d.layout;
  if (!layout_letter_ok(s->day_layout)) s->day_layout = d.day_layout;
  if (!layout_letter_ok(s->night_layout)) s->night_layout = d.night_layout;

  if (s->rain_um_per_tip < 10 || s->rain_um_per_tip > 10000) s->rain_um_per_tip = d.rain_um_per_tip;
  if (s->wind_mmps_per_hz < 10 || s->wind_mmps_per_hz > 100000) s->wind_mmps_per_hz = d.wind_mmps_per_hz;
  if (s->vane_offset_deg < -359 || s->vane_offset_deg > 359) s->vane_offset_deg = 0;
}

}  // namespace wx
