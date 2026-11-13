#include "wx/forecast.h"

#include <cstdio>
#include <cstring>

#include "wx/calendar.h"
#include "wx/json.h"

namespace wx {
namespace {

// "2026-11-12" → day number; also accepts "2026-11-12T06:41" (date part).
bool parse_date(const JsonSlice& s, int32_t* day) {
  if (s.n < 10 || s.p[4] != '-' || s.p[7] != '-') return false;
  int y = 0, m = 0, d = 0;
  for (int i = 0; i < 4; ++i) {
    if (s.p[i] < '0' || s.p[i] > '9') return false;
    y = y * 10 + (s.p[i] - '0');
  }
  for (int i = 5; i < 7; ++i) {
    if (s.p[i] < '0' || s.p[i] > '9') return false;
    m = m * 10 + (s.p[i] - '0');
  }
  for (int i = 8; i < 10; ++i) {
    if (s.p[i] < '0' || s.p[i] > '9') return false;
    d = d * 10 + (s.p[i] - '0');
  }
  if (m < 1 || m > 12 || d < 1 || d > 31) return false;
  *day = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
  return true;
}

// "2026-11-12T06:41" → minutes after midnight.
int16_t parse_hhmm(const JsonSlice& s) {
  if (s.n < 16 || s.p[10] != 'T' || s.p[13] != ':') return -1;
  int h = (s.p[11] - '0') * 10 + (s.p[12] - '0');
  int m = (s.p[14] - '0') * 10 + (s.p[15] - '0');
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return static_cast<int16_t>(h * 60 + m);
}

int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Ctx {
  Forecast* f;
  bool have_tmax[kForecastDays];
  bool have_tmin[kForecastDays];
  bool have_day[kForecastDays];
};

void on_value(void* user, const JsonSlice* path, int depth, int index, const JsonValue& v) {
  Ctx* c = static_cast<Ctx*>(user);
  Forecast* f = c->f;
  if (depth == 1) {
    if (path[0].equals("utc_offset_seconds") && v.type == JsonType::kNumber) {
      f->utc_offset_s = static_cast<int32_t>(clamp64(v.milli / 1000, -50400, 50400));
    } else if (path[0].equals("elevation") && v.type == JsonType::kNumber) {
      f->elevation_m = static_cast<int32_t>(clamp64(v.milli / 1000, -500, 9000));
    } else if (path[0].equals("timezone") && v.type == JsonType::kString) {
      size_t n = v.str.n < sizeof(f->timezone) - 1 ? v.str.n : sizeof(f->timezone) - 1;
      std::memcpy(f->timezone, v.str.p, n);
      f->timezone[n] = '\0';
    }
    return;
  }
  if (depth != 2) return;
  if (path[0].equals("current") && path[1].equals("weather_code") && v.type == JsonType::kNumber) {
    f->current_code = static_cast<uint8_t>(clamp64(v.milli / 1000, 0, 99));
    return;
  }
  if (!path[0].equals("daily") || index < 0 || index >= kForecastDays) return;
  ForecastDay& d = f->days[index];
  const JsonSlice& k = path[1];
  if (v.type == JsonType::kNull) return;  // missing value: keep default
  if (k.equals("time") && v.type == JsonType::kString) {
    c->have_day[index] = parse_date(v.str, &d.day);
  } else if (v.type == JsonType::kString) {
    if (k.equals("sunrise")) d.sunrise_min = parse_hhmm(v.str);
    if (k.equals("sunset")) d.sunset_min = parse_hhmm(v.str);
  } else if (v.type == JsonType::kNumber) {
    const int64_t x10 = v.milli / 100;
    if (k.equals("weather_code")) {
      d.code = static_cast<uint8_t>(clamp64(v.milli / 1000, 0, 99));
    } else if (k.equals("temperature_2m_max")) {
      d.tmax_c_x10 = static_cast<int16_t>(clamp64(x10, -900, 700));
      c->have_tmax[index] = true;
    } else if (k.equals("temperature_2m_min")) {
      d.tmin_c_x10 = static_cast<int16_t>(clamp64(x10, -900, 700));
      c->have_tmin[index] = true;
    } else if (k.equals("precipitation_probability_max")) {
      d.precip_prob = static_cast<uint8_t>(clamp64(v.milli / 1000, 0, 100));
    } else if (k.equals("precipitation_sum")) {
      d.precip_mm_x10 = static_cast<uint16_t>(clamp64(x10, 0, 65535));
    } else if (k.equals("wind_speed_10m_max")) {
      d.wind_max_mps_x10 = static_cast<uint16_t>(clamp64(x10, 0, 65535));
    }
  }
}

}  // namespace

size_t forecast_request_path(int32_t lat_e5, int32_t lon_e5, char* out, size_t cap) {
  auto deg = [](int32_t e5, char* buf, size_t n) {
    const bool neg = e5 < 0;
    const int32_t a = neg ? -e5 : e5;
    std::snprintf(buf, n, "%s%ld.%05ld", neg ? "-" : "", static_cast<long>(a / 100000),
                  static_cast<long>(a % 100000));
  };
  char la[16], lo[16];
  deg(lat_e5, la, sizeof(la));
  deg(lon_e5, lo, sizeof(lo));
  int n = std::snprintf(
      out, cap,
      "/v1/forecast?latitude=%s&longitude=%s&current=weather_code"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
      "precipitation_sum,wind_speed_10m_max,sunrise,sunset"
      "&forecast_days=%d&timezone=auto&temperature_unit=celsius&wind_speed_unit=ms"
      "&precipitation_unit=mm",
      la, lo, kForecastDays);
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

bool forecast_parse(const char* body, size_t n, Forecast* out) {
  std::memset(out, 0, sizeof(*out));
  for (auto& d : out->days) {
    d.sunrise_min = -1;
    d.sunset_min = -1;
  }
  Ctx c{};
  c.f = out;
  if (!json_walk(body, n, &on_value, &c)) {
    out->valid = false;
    return false;
  }
  int ndays = 0;
  while (ndays < kForecastDays && c.have_day[ndays] && c.have_tmax[ndays] && c.have_tmin[ndays]) {
    ++ndays;
  }
  out->ndays = static_cast<uint8_t>(ndays);
  out->valid = ndays > 0;
  return out->valid;
}

WeatherIcon weather_icon(uint8_t c) {
  if (c <= 1) return WeatherIcon::kSun;
  if (c == 2) return WeatherIcon::kPartly;
  if (c == 3) return WeatherIcon::kCloud;
  if (c == 45 || c == 48) return WeatherIcon::kFog;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WeatherIcon::kSnow;
  if (c >= 95) return WeatherIcon::kStorm;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return WeatherIcon::kRain;
  return WeatherIcon::kCloud;
}

const char* weather_text(uint8_t c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mostly sunny";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorms";
    case 96: case 99: return "Storms with hail";
    default: return "Cloudy";
  }
}

}  // namespace wx
