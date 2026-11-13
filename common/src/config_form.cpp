#include "wx/config_form.h"

#include <cstring>

#include "wx/http.h"
#include "wx/timezone.h"

namespace wx {
namespace {

bool get(const char* form, size_t len, const char* key, char* out, size_t cap) {
  return form_get(form, len, key, out, cap);
}

bool parse_layout(const char* v, uint8_t* out, bool allow_auto) {
  if (allow_auto && std::strcmp(v, "auto") == 0) {
    *out = kLayoutAuto;
    return true;
  }
  if (v[0] >= 'A' && v[0] <= 'D' && v[1] == '\0') {
    *out = static_cast<uint8_t>(v[0]);
    return true;
  }
  if (v[0] >= 'a' && v[0] <= 'd' && v[1] == '\0') {
    *out = static_cast<uint8_t>(v[0] - 'a' + 'A');
    return true;
  }
  return false;
}

ConfigResult fail(const char* msg) { return ConfigResult{false, msg, false, false, false}; }

}  // namespace

bool pin_ok(const Settings& s, const char* form, size_t len) {
  if (s.pin[0] == '\0') return true;
  char pin[16];
  if (!get(form, len, "pin", pin, sizeof(pin))) return false;
  return std::strcmp(pin, s.pin) == 0;
}

ConfigResult apply_config_form(Settings* out, const char* form, size_t len) {
  if (!pin_ok(*out, form, len)) return fail("Wrong PIN");

  Settings s = *out;
  ConfigResult r{true, nullptr, false, false, false};
  char v[80];

  if (get(form, len, "ssid", v, sizeof(v))) {
    if (std::strlen(v) > 32) return fail("WiFi name is too long (32 max)");
    if (std::strcmp(v, s.wifi_ssid) != 0) {
      copy_str(s.wifi_ssid, sizeof(s.wifi_ssid), v, std::strlen(v));
      r.wifi_changed = true;
    }
  }
  if (get(form, len, "pass", v, sizeof(v)) && v[0] != '\0') {
    const size_t n = std::strlen(v);
    if (n < 8 || n > 64) return fail("WiFi password must be 8 to 64 characters");
    if (std::strcmp(v, s.wifi_pass) != 0) {
      copy_str(s.wifi_pass, sizeof(s.wifi_pass), v, n);
      r.wifi_changed = true;
    }
  }
  if (get(form, len, "open_network", v, sizeof(v)) && std::strcmp(v, "1") == 0) {
    if (s.wifi_pass[0] != '\0') r.wifi_changed = true;
    s.wifi_pass[0] = '\0';
  }

  if (get(form, len, "hostname", v, sizeof(v))) {
    if (!hostname_valid(v)) return fail("Name may use letters, digits and '-' only (max 31)");
    if (std::strcmp(v, s.hostname) != 0) {
      copy_str(s.hostname, sizeof(s.hostname), v, std::strlen(v));
      r.hostname_changed = true;
    }
  }

  if (get(form, len, "tz", v, sizeof(v))) {
    TimeZone tz;
    if (!tz_parse(v, &tz)) return fail("Time zone not recognised");
    copy_str(s.tz, sizeof(s.tz), v, std::strlen(v));
  }

  char lat[24], lon[24];
  const bool has_lat = get(form, len, "lat", lat, sizeof(lat));
  const bool has_lon = get(form, len, "lon", lon, sizeof(lon));
  if (has_lat || has_lon) {
    if (lat[0] == '\0' && lon[0] == '\0') {
      if (s.have_location) r.location_changed = true;
      s.have_location = 0;
    } else {
      int32_t la = 0, lo = 0;
      if (!has_lat || !has_lon || !parse_fixed(lat, 5, &la) || !parse_fixed(lon, 5, &lo) ||
          la < -9000000 || la > 9000000 || lo < -18000000 || lo > 18000000) {
        return fail("Latitude/longitude must be decimal degrees, e.g. 39.74 and -104.99");
      }
      if (!s.have_location || la != s.lat_e5 || lo != s.lon_e5) r.location_changed = true;
      s.lat_e5 = la;
      s.lon_e5 = lo;
      s.have_location = 1;
    }
  }

  if (get(form, len, "elev", v, sizeof(v))) {
    if (v[0] == '\0') {
      s.have_elevation = 0;
    } else {
      int32_t e = 0;
      if (!parse_fixed(v, 2, &e)) return fail("Elevation must be a number");
      char unit[8] = "m";
      get(form, len, "elev_unit", unit, sizeof(unit));
      // e is in hundredths of the unit: hundredths of a metre are cm;
      // hundredths of a foot × 0.3048 are cm.
      const int64_t cm = std::strcmp(unit, "ft") == 0 ? static_cast<int64_t>(e) * 3048 / 10000 : e;
      if (cm < -50000 || cm > 900000) return fail("Elevation out of range");
      s.elevation_cm = static_cast<int32_t>(cm);
      s.have_elevation = 1;
    }
  }

  if (get(form, len, "temp_unit", v, sizeof(v))) {
    if (std::strcmp(v, "F") == 0) s.temp_unit = TempUnit::kF;
    else if (std::strcmp(v, "C") == 0) s.temp_unit = TempUnit::kC;
    else return fail("Unknown temperature unit");
  }
  if (get(form, len, "press_unit", v, sizeof(v))) {
    if (std::strcmp(v, "inHg") == 0) s.press_unit = PressUnit::kInHg;
    else if (std::strcmp(v, "hPa") == 0) s.press_unit = PressUnit::kHPa;
    else if (std::strcmp(v, "mmHg") == 0) s.press_unit = PressUnit::kMmHg;
    else return fail("Unknown pressure unit");
  }
  if (get(form, len, "wind_unit", v, sizeof(v))) {
    if (std::strcmp(v, "mph") == 0) s.wind_unit = WindUnit::kMph;
    else if (std::strcmp(v, "km/h") == 0) s.wind_unit = WindUnit::kKmh;
    else if (std::strcmp(v, "m/s") == 0) s.wind_unit = WindUnit::kMps;
    else if (std::strcmp(v, "kn") == 0) s.wind_unit = WindUnit::kKnots;
    else return fail("Unknown wind unit");
  }
  if (get(form, len, "rain_unit", v, sizeof(v))) {
    if (std::strcmp(v, "in") == 0) s.rain_unit = RainUnit::kIn;
    else if (std::strcmp(v, "mm") == 0) s.rain_unit = RainUnit::kMm;
    else return fail("Unknown rain unit");
  }

  if (get(form, len, "layout", v, sizeof(v)) && !parse_layout(v, &s.layout, true)) return fail("Unknown layout");
  if (get(form, len, "day_layout", v, sizeof(v)) && !parse_layout(v, &s.day_layout, false)) {
    return fail("Unknown day layout");
  }
  if (get(form, len, "night_layout", v, sizeof(v)) && !parse_layout(v, &s.night_layout, false)) {
    return fail("Unknown night layout");
  }

  int32_t n = 0;
  if (get(form, len, "rain_um", v, sizeof(v))) {
    if (!parse_fixed(v, 0, &n) || n < 10 || n > 10000) return fail("Rain per tip must be 10..10000 µm");
    s.rain_um_per_tip = static_cast<uint32_t>(n);
  }
  if (get(form, len, "wind_mmps", v, sizeof(v))) {
    if (!parse_fixed(v, 0, &n) || n < 10 || n > 100000) return fail("Anemometer factor out of range");
    s.wind_mmps_per_hz = static_cast<uint32_t>(n);
  }
  if (get(form, len, "vane_offset", v, sizeof(v))) {
    if (!parse_fixed(v, 0, &n) || n < -359 || n > 359) return fail("Vane offset must be -359..359");
    s.vane_offset_deg = static_cast<int16_t>(n);
  }

  if (get(form, len, "new_pin", v, sizeof(v))) {
    if (!pin_valid(v)) return fail("PIN must be 4 to 8 digits (or empty to remove it)");
    copy_str(s.pin, sizeof(s.pin), v, std::strlen(v));
  }

  settings_sanitize(&s);
  *out = s;
  return r;
}

void write_settings_json(const Settings& s, JsonWriter& w) {
  static const char* kTemp[] = {"F", "C"};
  static const char* kPress[] = {"inHg", "hPa", "mmHg"};
  static const char* kWind[] = {"mph", "km/h", "m/s", "kn"};
  static const char* kRain[] = {"in", "mm"};
  const char layout[2] = {static_cast<char>(s.layout), '\0'};
  const char day[2] = {static_cast<char>(s.day_layout), '\0'};
  const char night[2] = {static_cast<char>(s.night_layout), '\0'};

  w.begin_object();
  w.key("ssid").value(s.wifi_ssid);
  w.key("has_pass").boolean(s.wifi_pass[0] != '\0');
  w.key("hostname").value(s.hostname);
  w.key("tz").value(s.tz);
  w.key("lat");
  s.have_location ? w.fixed(s.lat_e5, 5) : w.value("");
  w.key("lon");
  s.have_location ? w.fixed(s.lon_e5, 5) : w.value("");
  w.key("elev_cm");
  s.have_elevation ? w.value(int64_t{s.elevation_cm}) : w.null();
  w.key("temp_unit").value(kTemp[static_cast<int>(s.temp_unit)]);
  w.key("press_unit").value(kPress[static_cast<int>(s.press_unit)]);
  w.key("wind_unit").value(kWind[static_cast<int>(s.wind_unit)]);
  w.key("rain_unit").value(kRain[static_cast<int>(s.rain_unit)]);
  w.key("layout").value(s.layout == kLayoutAuto ? "auto" : layout);
  w.key("day_layout").value(day);
  w.key("night_layout").value(night);
  w.key("rain_um").value(int64_t{s.rain_um_per_tip});
  w.key("wind_mmps").value(int64_t{s.wind_mmps_per_hz});
  w.key("vane_offset").value(int64_t{s.vane_offset_deg});
  w.key("has_pin").boolean(s.pin[0] != '\0');
  w.end_object();
}

}  // namespace wx
