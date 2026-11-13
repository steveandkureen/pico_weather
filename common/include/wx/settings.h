// Base station settings, edited on the /config page and saved in flash.
//
// Stored as the raw struct (the base station is the only reader and writer).
// RULE: only ever APPEND fields at the end. Loading an older, shorter record
// keeps the saved fields and fills newer ones with defaults, so a firmware
// update never loses the WiFi setup.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

enum class TempUnit : uint8_t { kF = 0, kC = 1 };
enum class PressUnit : uint8_t { kInHg = 0, kHPa = 1, kMmHg = 2 };
enum class WindUnit : uint8_t { kMph = 0, kKmh = 1, kMps = 2, kKnots = 3 };
enum class RainUnit : uint8_t { kIn = 0, kMm = 1 };

// Layout letters: 'A'..'D'. Layout setting 0 = Auto (day/night).
constexpr uint8_t kLayoutAuto = 0;

struct Settings {
  uint32_t version;  // bumped only if fields are ever removed/reinterpreted

  // Network
  char wifi_ssid[33];
  char wifi_pass[65];
  char hostname[32];  // mDNS name without ".local"

  // Location / time
  char tz[48];        // POSIX TZ string, e.g. "MST7MDT,M3.2.0,M11.1.0"
  int32_t lat_e5;     // degrees * 1e5
  int32_t lon_e5;
  uint8_t have_location;
  uint8_t have_elevation;
  uint8_t pad0[2];
  int32_t elevation_cm;

  // Display
  TempUnit temp_unit;
  PressUnit press_unit;
  WindUnit wind_unit;
  RainUnit rain_unit;
  uint8_t layout;        // 0 = Auto, else 'A'..'D'
  uint8_t day_layout;    // used by Auto between sunrise and sunset
  uint8_t night_layout;  // used by Auto otherwise
  uint8_t pad1;

  // Calibration
  uint32_t rain_um_per_tip;      // micrometres of rain per bucket tip (279 = 0.011")
  uint32_t wind_mmps_per_hz;     // mm/s of wind per anemometer pulse per second
  int16_t vane_offset_deg;       // added to the raw vane angle to get true direction
  uint16_t pad2;

  // Security
  char pin[9];  // empty = no PIN needed for /config changes

  // ---- append new fields below this line only ----
};

// Fills `s` with factory defaults.
void settings_defaults(Settings* s);

// Clamps every field to a legal value and guarantees string termination.
// Call after loading from flash and after applying form input.
void settings_sanitize(Settings* s);

// Hostname rule: 1..31 chars, letters/digits/'-', not starting or ending with '-'.
bool hostname_valid(const char* h);

// True if the PIN is acceptable: empty, or 4..8 digits.
bool pin_valid(const char* p);

// Copies at most cap-1 chars and always terminates.
void copy_str(char* dst, size_t cap, const char* src, size_t len);

}  // namespace wx
