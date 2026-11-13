// Calendar helpers: convert between a civil date and a day number
// (days since 1970-01-01). Uses Howard Hinnant's public-domain algorithms,
// which are exact for the proleptic Gregorian calendar.
#pragma once

#include <cstdint>

namespace wx {

struct Date {
  int32_t year;
  uint8_t month;  // 1..12
  uint8_t day;    // 1..31
};

constexpr int32_t days_from_civil(int32_t y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

constexpr Date civil_from_days(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return Date{y + (m <= 2 ? 1 : 0), static_cast<uint8_t>(m), static_cast<uint8_t>(d)};
}

}  // namespace wx
