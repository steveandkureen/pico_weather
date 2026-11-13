// Open-Meteo forecast: request path and response parser.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

constexpr int kForecastDays = 11;  // today + 10

struct ForecastDay {
  int32_t day;             // local day number (days since 1970-01-01)
  uint8_t code;            // WMO weather code
  uint8_t precip_prob;     // %
  int16_t tmax_c_x10;
  int16_t tmin_c_x10;
  uint16_t precip_mm_x10;
  uint16_t wind_max_mps_x10;
  int16_t sunrise_min;     // minutes after local midnight, -1 if unknown
  int16_t sunset_min;
};

struct Forecast {
  bool valid;
  uint8_t current_code;
  int32_t utc_offset_s;
  int32_t elevation_m;
  char timezone[40];       // IANA name, e.g. "America/Denver"
  uint8_t ndays;
  ForecastDay days[kForecastDays];
};

// Builds the request path (no host). Returns length, 0 if it didn't fit.
size_t forecast_request_path(int32_t lat_e5, int32_t lon_e5, char* out, size_t cap);

// Parses a response body. Requires at least today's entry with temperatures.
bool forecast_parse(const char* body, size_t n, Forecast* out);

// Small set of icons the page knows. Maps WMO codes.
enum class WeatherIcon : uint8_t { kSun, kPartly, kCloud, kFog, kRain, kSnow, kStorm };
WeatherIcon weather_icon(uint8_t wmo_code);
const char* weather_text(uint8_t wmo_code);

}  // namespace wx
